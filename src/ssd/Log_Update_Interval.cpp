#include "Log_Update_Interval.h"
#include "Sim_Defs.h"

#include "Flash_Block_Manager_MQ.h"

namespace SSD_Components{

    const uint32_t CON::ENTRY_UNIT = 100;
    const uint32_t CON::REQS_PER_INTERVAL = 16384;
    const lui_timestamp CON::TIMESTAMP_NOT_ACCESSED = UINT64_MAX;
    const uint8_t CON::HOT_FILTER_BITS_COUNT = 2;

    const uint8_t CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD = 5;
    const double CON::NOTICIBLE_REDUCTION_CRITERIA = 0.005;
    const double CON::UID_SELECTION_THRESHOLD = 0.005;

    const lui_timestamp CON::SELECT_EPOCH_TIMESTAMP = 1e6;

    HotFilter::HotFilter(uint64_t noOfBlocks)
    {
        filter = new uint64_t[noOfBlocks * CON::HOT_FILTER_BITS_COUNT / 64];
        for(uint32_t vector = 0; vector != noOfBlocks * CON::HOT_FILTER_BITS_COUNT / 64; vector++){
            filter[vector] = 0;
        }
        totalBlocksAge = 0;
        totalBlocksCount = 0;
    }

    HotFilter::~HotFilter()
    {
        delete[] filter;
    }


    double UID::createUID(const std::vector<uint64_t> &intervalCountTable, uint64_t totalReqs, uint64_t avgResTime, double lastBlocksAvgValidPagesRatio)
    {

        auto split = [&]() -> double {
            uint32_t& prevGroupSize = this->groupConf.back();
            this->groupConf.push_back(prevGroupSize);
            uint32_t& newGroupSize = this->groupConf.back(); 
            prevGroupSize = 0;

            double currentWAF = 0.0;
            double prevWAF = 0.0;

            while(true){
                prevGroupSize++;
                newGroupSize--;

                prevWAF = currentWAF;
                currentWAF = this->getWAF(intervalCountTable, totalReqs, avgResTime, lastBlocksAvgValidPagesRatio);
                if(currentWAF > prevWAF && !CON::IS_ZERO(prevWAF)){
                    prevGroupSize--;
                    newGroupSize++;
                    return prevWAF;
                }
                //In case of split isn't needed.
                else if(newGroupSize == 0){
                    this->groupConf.pop_back();
                    return 0.0;
                }
            }
            
        };

        double currentWAF = 0.0;
        double prevWAF = 0.0;
        uint8_t notNoticibleReductionCount = 0;

        while(true){
            prevWAF = currentWAF;
            currentWAF = split();
            if(CON::IS_ZERO(currentWAF)) break;

            if(!CON::IS_ZERO(prevWAF)){
                if(currentWAF > prevWAF * (1.0 - CON::NOTICIBLE_REDUCTION_CRITERIA)){
                    notNoticibleReductionCount++;
                } else{
                    notNoticibleReductionCount = 0;
                }

                if(notNoticibleReductionCount == CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD){
                    break;
                }
            }
        }

        return currentWAF;
    }
    
    double UID::getWAF(const std::vector<uint64_t>& intervalCountTable, uint64_t totalReqs, uint64_t avgResTime, double lastBlocksAvgValidPagesRatio)
    {
        double sumOfP = 0.0;
        std::vector<double> p;
        p.resize(groupConf.size(), 0.0);


        for(uint32_t intervalCountTableIdx = 0; intervalCountTableIdx < avgResTime; intervalCountTableIdx++){
            p.at(0) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
        }

        uint32_t currentWaitingPeriod = groupConf.at(1);
        uint32_t prevWaitingPeriod = 0;
        for(uint32_t groupNumber = 1; groupNumber < groupConf.size(); groupNumber++){
            for(uint32_t intervalCountTableIdx = prevWaitingPeriod; intervalCountTableIdx < currentWaitingPeriod; intervalCountTableIdx++){
                p.at(groupNumber) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
            }
            sumOfP += p.at(groupNumber);
            prevWaitingPeriod = currentWaitingPeriod;
            currentWaitingPeriod = groupConf.at(groupNumber) * (1.0 - sumOfP);
        }
        p.at(1) -= p.at(0);

        if(!CON::IS_ZERO(1.0 - sumOfP)){
            PRINT_ERROR("Sum of P is not 1 " << sumOfP)
        }

        return MarkovChain(p, lastBlocksAvgValidPagesRatio);
    }


    const double UID::MarkovChain(const std::vector<double>& p, double lastBlocksAvgValidPagesRatio)
    {
        uint32_t lastNodeIdx = p.size() - 1;
        uint32_t freeNodeIdx = p.size();
        double remainingP = 1.0;

        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.resize(p.size() + 1);

        //1. Free -> G(hot).
        //2. Free -> G(1).
        transitionProb.at(freeNodeIdx) = {p.at(0), 1.0 - p.at(0)};

        //All blocks are also assumed to be invali-dated in 𝐻𝑂𝑇.
        transitionProb.at(0) = {0, 1.0};
        remainingP -= p.at(0);

        for(int i = 1; i < transitionProb.size() - 1; i++){
            double curProb = (remainingP - p.at(i)) / remainingP;
            transitionProb.at(i) = {curProb, 1.0 - curProb};
            remainingP -= p.at(i);
        }
        transitionProb.at(lastNodeIdx) = {lastBlocksAvgValidPagesRatio, 1.0 - lastBlocksAvgValidPagesRatio};

        std::vector<double>* curNodes = new std::vector<double>();
        std::vector<double>* nextNodes = new std::vector<double>();
        curNodes->resize(p.size() + 1, 0.0);
        nextNodes->resize(p.size() + 1, 0.0);
        curNodes->at(freeNodeIdx) = 100.0;

        const int numIterations = 1000;
        for (int iter = 0; iter < numIterations; iter++) {
            nextNodes->at(0) = curNodes->at(freeNodeIdx) * transitionProb.at(freeNodeIdx).first;
            nextNodes->at(1) = curNodes->at(freeNodeIdx) * transitionProb.at(freeNodeIdx).second;
            for(uint32_t nodeIdx = 1; nodeIdx < lastNodeIdx; nodeIdx++){
                nextNodes->at(nodeIdx + 1) = curNodes->at(nodeIdx) * transitionProb.at(nodeIdx).first;
                nextNodes->at(freeNodeIdx) += curNodes->at(nodeIdx) * transitionProb.at(nodeIdx).second;
            }
            nextNodes->at(lastNodeIdx) += curNodes->at(lastNodeIdx) * transitionProb.at(lastNodeIdx).first;
            nextNodes->at(freeNodeIdx) += curNodes->at(lastNodeIdx) * transitionProb.at(lastNodeIdx).second;

            auto tmp = nextNodes;
            nextNodes = curNodes;
            curNodes = tmp;
            std::fill(nextNodes->begin(), nextNodes->end(), 0.0);
        }

        double numerator = 0.0;
        for(uint32_t nodeIdx = 0; nodeIdx < lastNodeIdx + 1; nodeIdx++){
            numerator += curNodes->at(nodeIdx);
        }
        double denominator = curNodes->at(0) + curNodes->at(1);

        delete curNodes, nextNodes;
        return (numerator / denominator);
    }

    Log_Update_Interval::Log_Update_Interval(uint64_t noOfBlocks, uint32_t pagesPerBlock)
    {
        this->pagesPerBlock = pagesPerBlock;
        requestCountInCurrentInterval = 0;
        currentTimestamp = 0;
        lastBlocksCount = 0;
        lastBlocksValidPagesCount = 0;

        hotFilter = new HotFilter(noOfBlocks);

        intervalCountTable.resize(CON::SELECT_EPOCH_TIMESTAMP);
        timestampTable.resize(noOfBlocks / CON::ENTRY_UNIT, CON::TIMESTAMP_NOT_ACCESSED);

        currentUID = NULL;
        changeUIDTag = true;
    }

    Log_Update_Interval::~Log_Update_Interval()
    {
        delete hotFilter;
        if(currentUID){
         delete currentUID;
        }
    }

    void Log_Update_Interval::addHotFilter(const LPA_type lba)
    {
        uint64_t& vector = hotFilter->filter[lba * CON::HOT_FILTER_BITS_COUNT / 64];
        uint8_t bit = (vector & (0b11 << ((lba * CON::HOT_FILTER_BITS_COUNT) % 64)));
        if(bit != ((1 << CON::HOT_FILTER_BITS_COUNT) - 1)){
            vector &= ~(0b11 << ((lba * CON::HOT_FILTER_BITS_COUNT) % 64));
            vector |= ((bit + 1) << ((lba * CON::HOT_FILTER_BITS_COUNT) % 64)); 
        }
    }

    void Log_Update_Interval::scheduleCurrentTimestamp()
    {
        requestCountInCurrentInterval++;
        if(requestCountInCurrentInterval == pagesPerBlock){
            currentTimestamp++;
            requestCountInCurrentInterval = 0;
        }
    }

    lui_timestamp& Log_Update_Interval::getTimestampEntry(const LPA_type lba)
    {
        if(!CON::ENTRY_VERIFY(lba)){
            PRINT_ERROR("GET TIMESTAMP ENTRY, LBA - " << lba);
        }
        return timestampTable.at(lba / CON::ENTRY_UNIT);
    }

    void Log_Update_Interval::updateTimestamp(const LPA_type lba)
    {
        if(!CON::ENTRY_VERIFY(lba)) return;
        lui_timestamp& prevTimestamp = getTimestampEntry(lba);

        if(prevTimestamp != CON::TIMESTAMP_NOT_ACCESSED){
            lui_timestamp timeInterval = currentTimestamp - prevTimestamp;
            intervalCountTable[timeInterval]++;
        }
        prevTimestamp = currentTimestamp;
    }

    void Log_Update_Interval::selectUID()
    {
        uint64_t totalReqs = 0;
        for(uint64_t intervalCount : intervalCountTable){
            totalReqs += intervalCount;
        }
        uint64_t avgResTime = (hotFilter->totalBlocksAge / hotFilter->totalBlocksCount);
        double lastBlocksAvgValidPagesRatio = (lastBlocksValidPagesCount / lastBlocksCount);

        UID* newUID = new UID();
        
        if(currentUID != NULL){
            double wafForCurUID = currentUID->getWAF(intervalCountTable, totalReqs, avgResTime, lastBlocksAvgValidPagesRatio);
            double wafForNewUID = newUID->createUID(intervalCountTable, totalReqs, avgResTime, lastBlocksAvgValidPagesRatio);
            
            if(wafForCurUID < (wafForNewUID * (1.0 - CON::UID_SELECTION_THRESHOLD))){
                return;
            }
        }
        delete currentUID;
        currentUID = newUID;
        changeUIDTag = true;
    }


    bool Log_Update_Interval::isHot(const LPA_type lba)
    {
        uint64_t& vector = hotFilter->filter[lba * CON::HOT_FILTER_BITS_COUNT / 64];
        uint8_t bit = (vector & (0b11 << ((lba * CON::HOT_FILTER_BITS_COUNT) % 64)));
        return (bit == ((1 << CON::HOT_FILTER_BITS_COUNT) - 1));
    }

    void Log_Update_Interval::writeData(const LPA_type lba)
    {
        scheduleCurrentTimestamp();
        addHotFilter(lba);
        updateTimestamp(lba);

        if((currentTimestamp % CON::SELECT_EPOCH_TIMESTAMP) == 0){
            selectUID();
        }
    }

    void Log_Update_Interval::addBlockAge(const Block_Type* block, const Queue_Type queueType)
    {
        if(queueType == Queue_Type::HOT_QUEUE){
            hotFilter->totalBlocksCount++;
            hotFilter->totalBlocksAge += (currentTimestamp - block->createTimestamp);
        }
        else if(queueType == Queue_Type::LAST_QUEUE){
            lastBlocksCount++;
            lastBlocksValidPagesCount += (pagesPerBlock - block->invalid_page_count);
        }
    }

    UID *Log_Update_Interval::getUID()
    {
        if(changeUIDTag){
            changeUIDTag = false;
            return currentUID;
        } else{
            return NULL;
        }
    }

    lui_timestamp Log_Update_Interval::getCurrentTimestamp()
    {
        return currentTimestamp;
    }
}