#include "Log_Update_Interval.h"
#include "Sim_Defs.h"
#include "Flash_Block_Manager_MQ.h"
#include <cmath>

namespace SSD_Components{

    const uint32_t CON::TIMETABLE_ENTRY_UNIT = 100;
    const lui_timestamp CON::TIMESTAMP_NOT_ACCESSED = UINT64_MAX;
    const uint8_t CON::HOT_FILTER_BITS_COUNT = 2;

    const lui_timestamp CON::UPDATE_INTERVAL_TABLE_SIZE = 176000;
    // const uint64_t CON::UPDATE_INTERVAL_TABLE_SIZE = 1e4;
    const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 176000;
    // const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 1e4;

    const double CON::NOTICIBLE_REDUCTION_CRITERIA = 0.005;
    const uint8_t CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD = 5;

    const double CON::UID_SELECTION_THRESHOLD = 0.005;

    const uint32_t CON::LIMITATION_GROUP_CONF = 20;

    HotFilter::HotFilter(uint64_t noOfPages)
    {
        vectorCount = ((noOfPages * 2) / 64);
        filter = new uint64_t[vectorCount];
    }

    HotFilter::~HotFilter()
    {
        delete[] filter;
    }

    void HotFilter::clearFilter()
    {
        for(int i = 0; i < vectorCount; i++){
            filter[i] = 0;
        }
    }

    uint8_t HotFilter::getFilter(const LPA_type lpa)
    {
        uint64_t& vector = filter[(lpa * 2) / 64];
        uint8_t bit = ((vector & (0b11 << ((lpa * 2) % 64))) >> ((lpa * 2) % 64));
        return bit;
    }

    void HotFilter::setFilter(const LPA_type lpa, const uint8_t newBit)
    {
        uint64_t& vector = filter[(lpa * 2) / 64];
        vector &= (0b00 << ((lpa * 2) % 64));
        vector |= (newBit << ((lpa * 2) % 64));
    }

    UID::UID(){
        groupConf = std::vector<uint32_t>();
    }

    UID::UID(const std::vector<uint32_t> &groupConf)
    :groupConf(groupConf) {}


    double UID::createUID(const std::map<uint64_t, uint64_t> &intervalCountTable, uint64_t totalReqs, uint32_t totalBlocksCount, uint32_t pagesPerBlock)
    {

        uint8_t notNoticibleReductionCount = 0;
        this->groupConf.push_back(totalBlocksCount);
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.push_back({0.0, 0.0});

        std::vector<uint32_t> optGroupConf;

        double hotTrafficRatio = intervalCountTable.at(0) / (double)totalReqs;
        UIDS startUIDS;
        startUIDS.lastItr = intervalCountTable.begin();
        startUIDS.sumOfP = 0.0;
        startUIDS.WAF = 10000.0;
        UIDS* prevUIDS = split(intervalCountTable, transitionProb, &startUIDS, totalReqs, hotTrafficRatio, true);
        prevUIDS->WAF = 10000.0;
        UIDS* newUIDS;
        while(true){
            newUIDS = nullptr;
            if(this->groupConf.back() <= 2 * MIN_QUEUE_SIZE || 
                this->groupConf.size() == CON::LIMITATION_GROUP_CONF){
                    break;
                }
            
            newUIDS = split(intervalCountTable, transitionProb, prevUIDS, totalReqs, hotTrafficRatio, false);
            if(newUIDS->WAF < prevUIDS->WAF){
                if(newUIDS->WAF < prevUIDS->WAF - CON::NOTICIBLE_REDUCTION_CRITERIA){
                    notNoticibleReductionCount = 0;
                } else{
                    notNoticibleReductionCount++;
                }
                delete prevUIDS;
                std::swap(newUIDS, prevUIDS);
                optGroupConf = this->groupConf;
            } else{
                notNoticibleReductionCount++;
            }
            if(notNoticibleReductionCount == CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD){
                break;
            }
        }
        this->groupConf = optGroupConf;
        double optWAF = prevUIDS->WAF;
        if(newUIDS) delete newUIDS;
        if(prevUIDS) delete prevUIDS;
        if(this->groupConf.back() == 0){
            this->groupConf.pop_back();
        }


        uint32_t* lastGroupConf = &groupConf.back();
        uint32_t* curGroupConf;
        for(uint32_t groupIdx = 0; groupIdx < groupConf.size() - 1; groupIdx++){
            curGroupConf = &groupConf.at(groupIdx);
            while((*curGroupConf) > MIN_QUEUE_SIZE){
                (*curGroupConf)--;
                (*lastGroupConf)++;
                double newWAF = getWAF(intervalCountTable, totalReqs);
                if(newWAF < optWAF){
                    optWAF = newWAF;
                } else{
                    (*curGroupConf)++;
                    (*lastGroupConf)--;
                    break;
                }
            }
        }

        return optWAF;
    }
    
    double UID::getWAF(const std::map<uint64_t, uint64_t>& intervalCountTable, uint64_t totalReqs)
    {
        // 4.4 Estimating Transition Probabilities.
        std::vector<double> p = std::vector<double>(groupConf.size(), 0.0);
        std::vector<uint32_t> waitingPeriod = std::vector<uint32_t>(groupConf.size(), 0);

        auto intervalCountTableItr = intervalCountTable.begin();

        double sumOfP = 0.0;
        waitingPeriod.at(0) = groupConf.at(0) / (1.0 - sumOfP) + 1;
        for(; intervalCountTableItr->first < waitingPeriod.at(0) && intervalCountTableItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; intervalCountTableItr++){
            p.at(0) += (double)(intervalCountTableItr->second) / (double)totalReqs;
        }
        sumOfP += p.at(0);

        for(int i = 1; i < groupConf.size(); i++){
            waitingPeriod.at(i) = (groupConf.at(i) / (1.0 - sumOfP)) + waitingPeriod.at(i - 1) + 1;
            for(; intervalCountTableItr->first < waitingPeriod.at(i) && intervalCountTableItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; intervalCountTableItr++){
                p.at(i) += ((double)intervalCountTableItr->second / (double)totalReqs);
            }
            sumOfP += p.at(i);
        }

        double lastP = 0.0;
        for(; intervalCountTableItr != intervalCountTable.end(); intervalCountTableItr++){
            lastP += ((double)intervalCountTableItr->second / (double)totalReqs);
        }
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.resize(groupConf.size());

        sumOfP = p.back() + lastP;
        transitionProb.back() = {lastP / sumOfP, 1.0 - (lastP / sumOfP)};
        for(int i = transitionProb.size() - 2; i >= 0; i--){
            transitionProb.at(i).first = sumOfP / (sumOfP + p.at(i));
            transitionProb.at(i).second = 1.0 - transitionProb.at(i).first;
            sumOfP += p.at(i);
        }

        double hotTrafficRatio = 0.0;
        if(intervalCountTable.find(0) != intervalCountTable.end()){
            hotTrafficRatio = ((double)intervalCountTable.at(0) / (double)totalReqs);
        }
        return MarkovChain(transitionProb, hotTrafficRatio);
    }

    double UID::MarkovChain(const std::vector<std::pair<double, double>>& transitionProb, double hotTrafficRatio)
    {
        // 4.3. Prediction of WAF using MCAM.

        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.

        //1. Free -> G(hot).
        //2. Free -> G(1).
        std::pair<double, double> freeNode = {hotTrafficRatio, 1.0 - hotTrafficRatio};
        std::vector<double> nodesCurEpoch;
        std::vector<double> nodesNextEpoch;
        nodesCurEpoch.resize(transitionProb.size(), 0.0);
        nodesNextEpoch.resize(transitionProb.size(), 0.0);

        double freeNodeCurEpoch = 100.0;
        double freeNodeNextEpoch = 0.0;

        const int numIterations = 1000;
        for (int iter = 0; iter < numIterations; iter++) {
            nodesNextEpoch.at(0) += freeNodeCurEpoch * freeNode.first;
            nodesNextEpoch.at(1) += freeNodeCurEpoch * freeNode.second;
            for(uint32_t nodeIdx = 0; nodeIdx < transitionProb.size() - 1; nodeIdx++){
                nodesNextEpoch.at(nodeIdx + 1) += nodesCurEpoch.at(nodeIdx) * transitionProb.at(nodeIdx).first;
                freeNodeNextEpoch += nodesCurEpoch.at(nodeIdx) * transitionProb.at(nodeIdx).second;
            }
            nodesNextEpoch.back() += nodesCurEpoch.back() * transitionProb.back().first;
            freeNodeNextEpoch += nodesCurEpoch.back() * transitionProb.back().second;

            std::swap(nodesCurEpoch, nodesNextEpoch);
            std::fill(nodesNextEpoch.begin(), nodesNextEpoch.end(), 0.0);

            auto tmp2 = freeNodeNextEpoch;
            freeNodeNextEpoch = freeNodeCurEpoch;
            freeNodeCurEpoch = tmp2;
            freeNodeNextEpoch = 0.0;

        }

        double userWrite = 0.0;
        double gcWrite = 0.0;
        // Hot and G1.
        if(groupConf.size() == 2){
            userWrite = freeNodeCurEpoch;
            gcWrite = nodesCurEpoch.at(1) - (freeNodeCurEpoch * hotTrafficRatio);
        }
        // Else.
        else{
            userWrite = nodesCurEpoch.at(0) + nodesCurEpoch.at(1);
            for(uint32_t nodeIdx = 2; nodeIdx < nodesCurEpoch.size(); nodeIdx++){
                gcWrite += nodesCurEpoch.at(nodeIdx);
            }
            gcWrite += nodesCurEpoch.at(0) * transitionProb.at(0).first;
        }
        return round(((userWrite + gcWrite) / userWrite) * 1e5) / 1e5;
    }

    //beforeItr은 이전 groupConfig의 마지막.
    UIDS* UID::split(const std::map<uint64_t, uint64_t> &intervalCountTable, std::vector<std::pair<double, double>>& transitionProb, const UIDS* lastUIDS, 
        uint32_t totalReqs, double hotTrafficRatio, bool isHot)
    {
        UIDS* optUIDS = new UIDS();
        optUIDS->WAF = 10000.0;
        // Group Initialize.
        this->groupConf.push_back(0);
        uint32_t& prevGroupConf = this->groupConf.at(this->groupConf.size() - 2);
        uint32_t optPrevGroupConf = prevGroupConf;
        uint32_t& nextGroupConf = this->groupConf.at(this->groupConf.size() - 1);
        prevGroupConf = MIN_QUEUE_SIZE - 1;
        nextGroupConf = optPrevGroupConf - MIN_QUEUE_SIZE + 1;

        uint32_t prevWaitingPeriod = 0;
        uint32_t nextWaitingPeriod = 0;

        // P initialize.
        double prevP = 0.0;
        double nextP = 0.0;
        double lastP = 0.0;
        auto prevItr = lastUIDS->lastItr; // Indicating the group divide line in interval count table.
        auto nextItr = lastUIDS->lastItr; // Indicating the group divide line in interval count table.
        while(CON::IS_ZERO(prevP) && nextGroupConf > MIN_QUEUE_SIZE){
            prevGroupConf++;
            nextGroupConf--;
            prevWaitingPeriod = ((double)prevGroupConf * (1.0 / (1.0 - lastUIDS->sumOfP))) + lastUIDS->lastItr->first + 1;
            for(; prevItr->first < prevWaitingPeriod && prevItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; prevItr++){
                prevP += (double)prevItr->second / (double)totalReqs;
            }
        }
        nextItr = prevItr;
        nextWaitingPeriod = ((double)nextGroupConf * (1.0 / (1.0 - (lastUIDS->sumOfP + prevP))) + prevWaitingPeriod) + 1;
        for(; nextItr->first < nextWaitingPeriod && nextItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; nextItr++){
            nextP += (double)nextItr->second / (double)totalReqs;
        }
        auto tmp = nextItr;
        for(; tmp != intervalCountTable.end(); tmp++){
            lastP += (double)tmp->second / (double)totalReqs;
        }

        double newWAF = 0.0;
        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.
        transitionProb.push_back({0.0, 0.0});
        std::pair<double, double>& prevTransitionProb = transitionProb.at(transitionProb.size() - 2);
        std::pair<double, double> optPrevTransitionProb = prevTransitionProb;
        std::pair<double, double>& nextTransitionProb = transitionProb.at(transitionProb.size() - 1);
        std::pair<double, double> optNextTransitionProb = optNextTransitionProb;
        while(true){
            // get transition probabilities.
            prevTransitionProb.first = (nextP + lastP) / (prevP + nextP + lastP);
            prevTransitionProb.second = (1.0 - prevTransitionProb.first);

            nextTransitionProb.first = lastP / (lastP + nextP);
            nextTransitionProb.second = (1.0 - nextTransitionProb.first);

            newWAF = MarkovChain(transitionProb, hotTrafficRatio);
            if(newWAF < optUIDS->WAF){
                optUIDS->WAF = newWAF;
                optUIDS->lastItr = prevItr;
                optUIDS->sumOfP = lastUIDS->sumOfP + prevP;
                optPrevGroupConf = prevGroupConf;
                optPrevTransitionProb = prevTransitionProb;
                optNextTransitionProb = nextTransitionProb;
            } else break;
            
            if(nextGroupConf <= MIN_QUEUE_SIZE) break;

            //get next size.
            double changeP = 0.0;
            while(CON::IS_ZERO(changeP)){
                if(nextGroupConf <= MIN_QUEUE_SIZE) break;
                prevGroupConf++;
                nextGroupConf--;
                prevWaitingPeriod = ((double)prevGroupConf * (1.0 / (1.0 - lastUIDS->sumOfP))) + lastUIDS->lastItr->first + 1;
                for(; prevItr->first < prevWaitingPeriod && prevItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; prevItr++){
                    changeP = ((double)prevItr->second / (double)totalReqs);
                }
                prevP += changeP;
                nextP -= changeP;
                nextP = CON::IS_ZERO(nextP) ? 0.0 : nextP;
            }

            changeP = 0.0;
            nextWaitingPeriod = ((double)nextGroupConf * (1.0 / (1.0 - lastUIDS->sumOfP + prevP))) + prevWaitingPeriod + 1;
            for(; nextItr->first < nextWaitingPeriod && nextItr->first < CON::UPDATE_INTERVAL_TABLE_SIZE - 1; nextItr++){
                changeP += ((double)nextItr->second / (double)totalReqs);
            }
            nextP += changeP;
            lastP -= changeP;
            lastP = CON::IS_ZERO(lastP) ? 0.0 : lastP;
        }
        
        uint32_t totalGroupConf = prevGroupConf + nextGroupConf;
        prevGroupConf = optPrevGroupConf;
        nextGroupConf = totalGroupConf - optPrevGroupConf;
        prevTransitionProb = optPrevTransitionProb;
        nextTransitionProb = optNextTransitionProb;
        return optUIDS;
    }

    Log_Update_Interval::Log_Update_Interval(uint64_t totalBlocksCount, uint32_t pagesPerBlock, const std::vector<uint32_t>& initialGroupConf)
    {
        this->pagesPerBlock = pagesPerBlock;
        requestCountInCurrentInterval = 0;
        currentTimestamp = 1;
        totalHotBlocksAge = 0;
        totalErasedHotBlocksCount = 0;
        totalErasedLastBlocksCount = 0;
        totalErasedLastBlocksValidPagesCount = 0;
        totalHotBlocksValidPages = 0;
        hotFilter = new HotFilter(totalBlocksCount * pagesPerBlock);

        currentUID = NULL;
        changeUIDTag = false;

        currentUID = new UID(initialGroupConf);
        this->totalBlocksCount = totalBlocksCount;
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
        uint8_t bit = hotFilter->getFilter(lba);
        return (bit == 3);
    }

    void Log_Update_Interval::updateHotFilter(const LPA_type lba, const lui_timestamp blkAge, const level_type level, const bool forGC)
    {
        uint8_t bit = hotFilter->getFilter(lba);

        if(forGC){
            if(bit > 0){
                bit--;
                if(level == 0) bit--;
            }
        } else{
            if(level == 1){
                if((totalErasedHotBlocksCount == 0) || (blkAge < (totalHotBlocksAge / totalErasedHotBlocksCount))){
                    if(bit != 3){
                        bit++;
                    }
                } else if(bit > 0){
                    bit--;
                }
            }
        }

        hotFilter->setFilter(lba, bit);
    }

    void Log_Update_Interval::updateTable(const LPA_type lba)
    {
        scheduleCurrentTimestamp();
        setTables(lba);

        if((currentTimestamp % CON::GROUP_CONFIGURE_EPOCH) == 0 && (requestCountInCurrentInterval == 0)){
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
        if(timestampTable.find(lba / CON::TIMETABLE_ENTRY_UNIT) == timestampTable.end()){
            timestampTable[lba / CON::TIMETABLE_ENTRY_UNIT] = CON::TIMESTAMP_NOT_ACCESSED;
        }
        lui_timestamp& prevTimestamp = timestampTable[lba / CON::TIMETABLE_ENTRY_UNIT];

        if(prevTimestamp == CON::TIMESTAMP_NOT_ACCESSED){
            prevTimestamp = (currentTimestamp << 1) + 1;
        } else{
            lui_timestamp timeInterval = (currentTimestamp - (prevTimestamp >> 1));
            if(timeInterval > CON::UPDATE_INTERVAL_TABLE_SIZE - 1){
                timeInterval = CON::UPDATE_INTERVAL_TABLE_SIZE - 1;
            }
            updateIntervalTable[timeInterval]++;
            
            prevTimestamp = (currentTimestamp << 1);
        }
    }

    // Not running.
    void Log_Update_Interval::addBlockAge(const Block_Type* block, const Queue_Type queueType)
    {
        if(queueType == Queue_Type::HOT_QUEUE){
            totalErasedHotBlocksCount++;
            totalHotBlocksAge += (currentTimestamp - block->createTimestamp);
            totalHotBlocksValidPages += pagesPerBlock - block->invalid_page_count;
        }
        else if(queueType == Queue_Type::LAST_QUEUE){
            totalErasedLastBlocksCount++;
            totalErasedLastBlocksValidPagesCount += (pagesPerBlock - block->invalid_page_count);
        }
    }

    void Log_Update_Interval::clearTable()
    {
        updateIntervalTable.clear();
        timestampTable.clear();
    }

    void Log_Update_Interval::selectUID()
    {
        uint64_t totalReqs = 0;
        PRINT_MESSAGE("Start group configuration...")
        for(auto updateIntervalTableEntry : updateIntervalTable){
            totalReqs += updateIntervalTableEntry.second;
        }

        updateIntervalTable[CON::UPDATE_INTERVAL_TABLE_SIZE - 1] = 0;
        for(auto& timeTableEntry : timestampTable){
            //first access.
            if((timeTableEntry.second & 1) == 1){
                updateIntervalTable[CON::UPDATE_INTERVAL_TABLE_SIZE - 1]++;
                totalReqs++;
            }
        }

        UID* newUID = new UID();
        double wafForNewUID = newUID->createUID(updateIntervalTable, totalReqs, totalBlocksCount, pagesPerBlock);
        double wafForCurUID = currentUID->getWAF(updateIntervalTable, totalReqs);
        PRINT_MESSAGE("Current : " << wafForCurUID << "\tNew : " << wafForNewUID)
        PRINT_MESSAGE("Current UID is...")
        for(uint32_t i = 0; i < currentUID->groupConf.size(); i++){
            PRINT_MESSAGE(i << "\t" << currentUID->groupConf.at(i))
        }
        PRINT_MESSAGE("------------------------------------------------")
        PRINT_MESSAGE("New UID is...")
        for(uint32_t i = 0; i < newUID->groupConf.size(); i++){
            PRINT_MESSAGE(i << "\t" << newUID->groupConf.at(i))
        }
        PRINT_MESSAGE("------------------------------------------------")
        // if(true){
        if(wafForCurUID > (wafForNewUID * (1.0 - CON::UID_SELECTION_THRESHOLD))){
            delete currentUID;
            currentUID = newUID;
            changeUIDTag = true;
            PRINT_MESSAGE("new UID has been selected....")
        } else{
            delete newUID;
            PRINT_MESSAGE("new UID hasn't been selected....")
        }
        clearTable();
        hotFilter->clearFilter();
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