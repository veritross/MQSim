#include "Log_Update_Interval.h"
#include "Sim_Defs.h"

#include "Flash_Block_Manager_MQ.h"

namespace SSD_Components{

    const uint32_t CON::TIMETABLE_ENTRY_UNIT = 100;
    const lui_timestamp CON::TIMESTAMP_NOT_ACCESSED = UINT64_MAX;
    const uint8_t CON::HOT_FILTER_BITS_COUNT = 2;

    const uint64_t CON::UPDATE_INTERVAL_TABLE_SIZE = 1e5;

    HotFilter::HotFilter(uint64_t noOfBlocks)
    {
        filter = new uint64_t[noOfBlocks * CON::HOT_FILTER_BITS_COUNT / 64];
        for(uint32_t vector = 0; vector != noOfBlocks * CON::HOT_FILTER_BITS_COUNT / 64; vector++){
            filter[vector] = 0;
        }
    }

    HotFilter::~HotFilter()
    {
        delete[] filter;
    }

    const double CON::NOTICIBLE_REDUCTION_CRITERIA = 0.005;
    const uint8_t CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD = 5;
    double UID::createUID(const std::vector<uint64_t> &intervalCountTable, uint64_t totalReqs, uint64_t hotAvgResTime, double lastBlocksAvgValidPagesRatio)
    {
        auto split = [&]() -> double {
            uint32_t& prevGroupSize = this->groupConf.back();
            this->groupConf.push_back(prevGroupSize);
            uint32_t& newGroupSize = this->groupConf.back(); 
            prevGroupSize = 0;

            double curWAF = 0.0;
            double prevWAF = 0.0;

            while(true){
                prevGroupSize++;
                newGroupSize--;

                prevWAF = curWAF;
                curWAF = this->getWAF(intervalCountTable, totalReqs, hotAvgResTime, lastBlocksAvgValidPagesRatio);
                if((curWAF > prevWAF) && !CON::IS_ZERO(prevWAF)){
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

        double curWAF = 0.0;
        double prevWAF = 0.0;
        uint8_t notNoticibleReductionCount = 0;

        while(true){
            prevWAF = curWAF;
            curWAF = split();
            if(CON::IS_ZERO(curWAF)) break;

            if(!CON::IS_ZERO(prevWAF)){
                if(curWAF > prevWAF * (1.0 - CON::NOTICIBLE_REDUCTION_CRITERIA)){
                    notNoticibleReductionCount++;
                } else{
                    notNoticibleReductionCount = 0;
                }

                if(notNoticibleReductionCount == CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD){
                    break;
                }
            }
        }

        return curWAF;
    }
    
    double UID::getWAF(const std::vector<uint64_t>& intervalCountTable, uint64_t totalReqs, uint64_t hotAvgResTime, double lastBlocksAvgValidPagesRatio)
    {
        // 4.4 Estimating Transition Probabilities.
        double sumOfP = 0.0;
        std::vector<double> p;
        p.resize(groupConf.size(), 0.0);

        uint32_t intervalCountTableIdx = 0;

        // Hot P.
        for(; intervalCountTableIdx < hotAvgResTime; intervalCountTableIdx++){
            p.at(0) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
        }
        sumOfP += p.at(0);

        uint32_t waitingPeriod = intervalCountTableIdx;
        for(uint32_t groupNumber = 1; groupNumber < groupConf.size(); groupNumber++){
            waitingPeriod = groupConf.at(groupNumber) / (1.0 - sumOfP);
            for(; intervalCountTableIdx < waitingPeriod; intervalCountTableIdx++){
                p.at(groupNumber) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
            }
            sumOfP += p.at(groupNumber);
        }

        if(!CON::IS_ZERO(1.0 - sumOfP)){
            PRINT_ERROR("Sum of P is not 1 but " << sumOfP)
        }

        return MarkovChain(p, lastBlocksAvgValidPagesRatio);
    }


    const double UID::MarkovChain(const std::vector<double>& p, double lastBlocksAvgValidPagesRatio)
    {
        // 4.3. Prediction of WAF using MCAM.
        double remainingP = 1.0;

        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.resize(p.size());

        //1. Free -> G(hot).
        //2. Free -> G(1).
        std::pair<double, double> freeNode = {p.at(0), 1.0 - p.at(0)};

        //All blocks are also assumed to be invali-dated in 𝐻𝑂𝑇.
        transitionProb.at(0) = {0, 1.0};
        remainingP -= p.at(0);

        for(int i = 1; i < transitionProb.size() - 1; i++){
            double probToNextGroup = (remainingP - p.at(i)) / remainingP;
            transitionProb.at(i) = {probToNextGroup, 1.0 - probToNextGroup};
            remainingP -= p.at(i);
        }
        transitionProb.back() = {lastBlocksAvgValidPagesRatio, 1.0 - lastBlocksAvgValidPagesRatio};

        std::vector<double>* nodesCurEpoch = new std::vector<double>();
        std::vector<double>* nodesNextEpoch = new std::vector<double>();
        nodesCurEpoch->resize(p.size(), 0.0);
        nodesNextEpoch->resize(p.size(), 0.0);

        double freeNodeCurEpoch = 100.0;
        double freeNodeNextEpoch = 0.0;

        const int numIterations = 1000;
        for (int iter = 0; iter < numIterations; iter++) {
            nodesNextEpoch->at(0) = freeNodeCurEpoch * freeNode.first;
            nodesNextEpoch->at(1) = freeNodeCurEpoch * freeNode.second;
            for(uint32_t nodeIdx = 1; nodeIdx < transitionProb.size() - 1; nodeIdx++){
                nodesNextEpoch->at(nodeIdx + 1) = nodesNextEpoch->at(nodeIdx) * transitionProb.at(nodeIdx).first;
                freeNodeNextEpoch += nodesCurEpoch->at(nodeIdx) * transitionProb.at(nodeIdx).second;
            }
            nodesNextEpoch->back() += nodesCurEpoch->back() * transitionProb.back().first;
            freeNodeNextEpoch += nodesCurEpoch->back() * transitionProb.back().second;

            auto tmp = nodesNextEpoch;
            nodesNextEpoch = nodesCurEpoch;
            nodesCurEpoch = tmp;
            std::fill(nodesNextEpoch->begin(), nodesNextEpoch->end(), 0.0);

            auto tmp2 = freeNodeNextEpoch;
            freeNodeNextEpoch = freeNodeCurEpoch;
            freeNodeCurEpoch = tmp2;
            freeNodeNextEpoch = 0.0;
        }

        double numerator = 0.0;
        for(uint32_t nodeIdx = 0; nodeIdx < nodesCurEpoch->size(); nodeIdx++){
            numerator += nodesCurEpoch->at(nodeIdx);
        }

        double denominator = nodesCurEpoch->at(0) + nodesCurEpoch->at(1);

        delete nodesCurEpoch, nodesNextEpoch;
        return (numerator / denominator);
    }

    Log_Update_Interval::Log_Update_Interval(uint64_t noOfBlocks, uint32_t pagesPerBlock)
    {
        this->pagesPerBlock = pagesPerBlock;
        requestCountInCurrentInterval = 0;
        currentTimestamp = 0;
        totalErasedLastBlocksCount = 0;
        totalErasedLastBlocksValidPagesCount = 0;

        hotFilter = new HotFilter(noOfBlocks);

        updateIntervalTable.resize(CON::UPDATE_INTERVAL_TABLE_SIZE);
        timestampTable.resize(noOfBlocks / CON::TIMETABLE_ENTRY_UNIT, CON::TIMESTAMP_NOT_ACCESSED);

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

    bool Log_Update_Interval::isHot(const LPA_type lba)
    {
        uint64_t filterIdx = lba * CON::HOT_FILTER_BITS_COUNT;
        uint8_t bitMask = ((1 << CON::HOT_FILTER_BITS_COUNT) - 1);
        uint64_t& vector = hotFilter->filter[filterIdx / 64];
        uint8_t bit = ((vector & (bitMask << (filterIdx % 64))) >> (filterIdx % 64));

        return (bit == bitMask);
    }

    void Log_Update_Interval::updateHotFilter(const LPA_type lba, const lui_timestamp blkAge, const level_type level, const bool forGC)
    {
        if(totalErasedHotBlocksCount == 0) return;

        uint64_t filterIdx = lba * CON::HOT_FILTER_BITS_COUNT;
        uint64_t& vector = hotFilter->filter[filterIdx / 64];

        uint8_t bitMask = ((1 << CON::HOT_FILTER_BITS_COUNT) - 1);
        uint8_t bit = ((vector & (bitMask << (filterIdx % 64))) >> (filterIdx % 64));

        if(forGC){
            if(bit > 0){
                bit--;
                if(level == 0) bit--;
            }
        } else{
            if(level == 1){
                if((blkAge < (totalHotBlocksAge / totalErasedHotBlocksCount))){
                    if(bit != bitMask){
                        bit++;
                    }
                } else if(bit > 0){
                    bit--;
                }
            }
        }
        vector &= ~(bitMask << (filterIdx % 64));
        vector |= ((bit + 1) << ((filterIdx % 64)));
    }

    const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 1e6;
    void Log_Update_Interval::updateTable(const LPA_type lba)
    {
        scheduleCurrentTimestamp();
        setTables(lba);

        if((currentTimestamp % CON::GROUP_CONFIGURE_EPOCH) == 0){
            selectUID();
        }
    }

    //Current Timestamp is increased when user write pages as amount of a block.
    void Log_Update_Interval::scheduleCurrentTimestamp()
    {
        requestCountInCurrentInterval++;
        if(requestCountInCurrentInterval == pagesPerBlock){
            currentTimestamp++;
            requestCountInCurrentInterval = 0;
        }
    }

    //Timestamp table, update interval table;
    void Log_Update_Interval::setTables(const LPA_type lba)
    {
        if(!CON::ENTRY_VERIFY(lba)) return;
        lui_timestamp& prevTimestamp = timestampTable[lba / CON::TIMETABLE_ENTRY_UNIT];

        if(prevTimestamp != CON::TIMESTAMP_NOT_ACCESSED){
            lui_timestamp timeInterval = (currentTimestamp - prevTimestamp);
            if(timeInterval >= CON::UPDATE_INTERVAL_TABLE_SIZE){
                timeInterval = CON::UPDATE_INTERVAL_TABLE_SIZE - 1;
            }
            updateIntervalTable[timeInterval]++;
        }
        prevTimestamp = currentTimestamp;
    }

    void Log_Update_Interval::addBlockAge(const Block_Type* block, const Queue_Type queueType)
    {
        if(queueType == Queue_Type::HOT_QUEUE){
            totalErasedHotBlocksCount++;
            totalHotBlocksAge += (currentTimestamp - block->createTimestamp);
        }
        else if(queueType == Queue_Type::LAST_QUEUE){
            totalErasedLastBlocksCount++;
            totalErasedLastBlocksValidPagesCount += (pagesPerBlock - block->invalid_page_count);
        }
    }

    const double CON::UID_SELECTION_THRESHOLD = 0.005;
    void Log_Update_Interval::selectUID()
    {
        uint64_t totalReqs = CON::GROUP_CONFIGURE_EPOCH;
        uint64_t hotAvgResTime = (totalHotBlocksAge / totalErasedHotBlocksCount);
        double lastBlocksAvgValidPagesRatio = (totalErasedLastBlocksValidPagesCount / totalErasedLastBlocksCount);

        UID* newUID = new UID();
        
        if(currentUID != NULL){
            double wafForCurUID = currentUID->getWAF(updateIntervalTable, totalReqs, hotAvgResTime, lastBlocksAvgValidPagesRatio);
            double wafForNewUID = newUID->createUID(updateIntervalTable, totalReqs, hotAvgResTime, lastBlocksAvgValidPagesRatio);
            
            if(wafForCurUID < (wafForNewUID * (1.0 - CON::UID_SELECTION_THRESHOLD))){
                return;
            }
        }
        delete currentUID;
        currentUID = newUID;
        changeUIDTag = true;
    }

    // Only return UID pointer if a new UID has been created.
    // Else, return the null pointer.
    UID *Log_Update_Interval::getUID()
    {
        if(changeUIDTag){
            changeUIDTag = false;
            return currentUID;
        } else{
            return nullptr;
        }
    }

    lui_timestamp Log_Update_Interval::getCurrentTimestamp()
    {
        return currentTimestamp;
    }
}