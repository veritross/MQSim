#include "Log_Update_Interval.h"
#include "Sim_Defs.h"
#include "Flash_Block_Manager_MQ.h"
#include <cmath>

namespace SSD_Components{

    const uint32_t CON::TIMETABLE_ENTRY_UNIT = 100;
    const lui_timestamp CON::TIMESTAMP_NOT_ACCESSED = UINT64_MAX;
    const uint8_t CON::HOT_FILTER_BITS_COUNT = 2;

    const lui_timestamp CON::UPDATE_INTERVAL_TABLE_SIZE = 4 * 1e4;
    // const uint64_t CON::UPDATE_INTERVAL_TABLE_SIZE = 1e4;
    const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 4 * 1e4;
    // const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 1e4;

    const double CON::NOTICIBLE_REDUCTION_CRITERIA = 0.005;
    const uint8_t CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD = 5;

    const double CON::UID_SELECTION_THRESHOLD = 0.005;

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


    double UID::createUID(const std::map<uint64_t, uint64_t> &intervalCountTable, uint64_t totalReqs, uint32_t totalBlocksCount, uint32_t pagesPerBlock, double avgBlocksResTime)
    {
        uint8_t notNoticibleReductionCount = 0;
        this->groupConf.push_back(totalBlocksCount);

        double curWAF = 1e5;
        double newWAF = 1e5;
        while(this->groupConf.back() > 2 * MIN_QUEUE_SIZE){
            double curInnerWAF = 1e5;
            double newInnerWAF = 1e5;
            this->groupConf.push_back(this->groupConf.back());
            uint32_t& prevGroupCount = this->groupConf.at(this->groupConf.size() - 2);
            uint32_t& nextGroupCount = this->groupConf.back();
            prevGroupCount = MIN_QUEUE_SIZE;
            nextGroupCount -= prevGroupCount;
            while(nextGroupCount > MIN_QUEUE_SIZE){
                newInnerWAF = getWAF(intervalCountTable, totalReqs, avgBlocksResTime, pagesPerBlock);
                if(curInnerWAF > newInnerWAF){
                    curInnerWAF = newInnerWAF;
                    prevGroupCount++;
                    nextGroupCount--;
                } else{
                    prevGroupCount--;
                    nextGroupCount++;
                    newWAF = curInnerWAF;
                    break;
                }
            }
            
            if(curWAF > newWAF){
                if(curWAF > (newWAF + CON::NOTICIBLE_REDUCTION_CRITERIA)){
                    notNoticibleReductionCount = 0;
                } else{
                    notNoticibleReductionCount++;
                }
                curWAF = newWAF;
                if(notNoticibleReductionCount == CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD){
                    break;
                }
            } else if(this->groupConf.size() > 3){
                break;
            }
        }

        // Phase 2.
        for(int groupIdx = 0; groupIdx < groupConf.size() - 1; groupIdx++){
            uint32_t& victimGroup = this->groupConf.at(groupIdx);
            while(victimGroup >= MIN_QUEUE_SIZE){
                victimGroup--;
                this->groupConf.back()++;
                newWAF = getWAF(intervalCountTable, totalReqs, avgBlocksResTime, pagesPerBlock);
                if(curWAF > newWAF){
                    curWAF = newWAF;
                } else{
                    victimGroup++;
                    this->groupConf.back()--;
                    break;
                }
            }
        }

        for(auto itr = groupConf.begin() + 1; itr != groupConf.end() - 1;){
            if(*itr <= MIN_QUEUE_SIZE){
                groupConf.back() += *itr;
                itr = groupConf.erase(itr);
            } else{
                itr++;
            }
        }

        return curWAF;
    }
    
    double UID::getWAF(const std::map<uint64_t, uint64_t>& intervalCountTable, uint64_t totalReqs, double avgBlocksResTime, uint32_t pagesPerBlock)
    {
        // 4.4 Estimating Transition Probabilities.
        std::vector<double> p = std::vector<double>(groupConf.size(), 0.0);
        std::vector<uint32_t> waitingPeriod = std::vector<uint32_t>(groupConf.size(), 0);
        auto intervalCountTableItr = intervalCountTable.begin();
        auto lastGroupItr = intervalCountTable.begin();
        double sumOfP = 0.0;
        
        
        waitingPeriod.at(0) = groupConf.at(0);
        while(intervalCountTableItr->first <= waitingPeriod.at(0) && intervalCountTableItr != intervalCountTable.end()){
            p.at(0) += (double)intervalCountTableItr->second / (double)totalReqs;
            intervalCountTableItr++;
        }
        sumOfP += p.at(0);
        if(groupConf.size() == 2){
            lastGroupItr = intervalCountTableItr;
        }

        waitingPeriod.at(1) = (groupConf.at(0) + groupConf.at(1)) * (1.0 / (1.0 - sumOfP));
        while(intervalCountTableItr->first <= waitingPeriod.at(1) && intervalCountTableItr != intervalCountTable.end()){
            p.at(1) += (double)intervalCountTableItr->second / (double)totalReqs;
            intervalCountTableItr++;
        }
        sumOfP += p.at(0);

        for(uint32_t groupIdx = 2; groupIdx < groupConf.size() - 1; groupIdx++){
            waitingPeriod.at(groupIdx) = waitingPeriod.at(groupIdx - 1) + groupConf.at(groupIdx) * (1.0 / (1.0 - sumOfP));
            while(intervalCountTableItr->first <= waitingPeriod.at(groupIdx) && intervalCountTableItr != intervalCountTable.end()){
                p.at(groupIdx) += (double)intervalCountTableItr->second / (double)totalReqs;
                intervalCountTableItr++;
            }
            sumOfP += p.at(groupIdx);
        }

        if(groupConf.size() != 2){
            lastGroupItr = intervalCountTableItr;
        }

        waitingPeriod.back() = CON::UPDATE_INTERVAL_TABLE_SIZE - 1;
        while(intervalCountTableItr != intervalCountTable.end()){
            p.back() += (double)intervalCountTableItr->second / (double)totalReqs;
            intervalCountTableItr++;
        }
        
        if(lastGroupItr == intervalCountTable.end()) return 1e6;
        
        double lastWAF = getLastGroupWAF(intervalCountTable, pagesPerBlock, lastGroupItr);

        double freeToHot = 0.;
        for(auto itr = intervalCountTable.begin(); itr->first <= avgBlocksResTime; itr++){
            freeToHot += (double)itr->second / (double)totalReqs;
        }


        return MarkovChain(p, freeToHot, lastWAF);
    }

    double UID::MarkovChain(const std::vector<double>& p, const double freeToHot, double lastGroupWAF)
    {
        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.resize(p.size());
        transitionProb.at(0) = {0.0, 1.0};
        for(uint32_t groupIdx = 1; groupIdx < p.size() - 1; groupIdx++){
            double probToNext = (p.at(groupIdx + 1) / (p.at(groupIdx) + p.at(groupIdx + 1)));
            transitionProb.at(groupIdx) = {probToNext, 1.0 - probToNext};
        }
        //Set Last Transition Probability.
        transitionProb.back().first = 1.0 - (1.0 / lastGroupWAF);
        transitionProb.back().second = 1.0 - transitionProb.back().first;

        //1. Free -> G(hot).
        //2. Free -> G(1).
        std::pair<double, double> freeTransitionProb = {freeToHot, 1.0 - freeToHot};

        std::vector<double> nodesCurEpoch;
        std::vector<double> nodesNextEpoch;
        nodesCurEpoch.resize(p.size(), 0.0);
        nodesNextEpoch.resize(p.size(), 0.0);

        double freeNodeCurEpoch = 1e6;
        double freeNodeNextEpoch = 0.0;

        const int numIterations = 1000;
        for (int iter = 0; iter < numIterations; iter++) {
            nodesNextEpoch.at(0) += freeNodeCurEpoch * freeTransitionProb.first;
            nodesNextEpoch.at(1) += freeNodeCurEpoch * freeTransitionProb.second;
            for(uint32_t nodeIdx = 0; nodeIdx < transitionProb.size() - 1; nodeIdx++){
                nodesNextEpoch.at(nodeIdx + 1) += nodesCurEpoch.at(nodeIdx) * transitionProb.at(nodeIdx).first;
                freeNodeNextEpoch += nodesCurEpoch.at(nodeIdx) * transitionProb.at(nodeIdx).second;
            }
            nodesNextEpoch.back() += nodesCurEpoch.back() * transitionProb.back().first;
            freeNodeNextEpoch += nodesCurEpoch.back() * transitionProb.back().second;

            nodesCurEpoch = nodesNextEpoch;
            std::fill(nodesNextEpoch.begin(), nodesNextEpoch.end(), 0.0);

            freeNodeCurEpoch = freeNodeNextEpoch;
            freeNodeNextEpoch = 0.0;
        }

        double userWrite = 0.0;
        double gcWrite = 0.0;
        // Hot and G1.
        if(groupConf.size() == 2){
            userWrite = freeNodeCurEpoch;
            gcWrite = nodesCurEpoch.at(0) + nodesCurEpoch.at(1);
        }
        // Else.
        else{
            userWrite = nodesCurEpoch.at(0) + nodesCurEpoch.at(1);
            for(uint32_t nodeIdx = 2; nodeIdx < nodesCurEpoch.size(); nodeIdx++){
                gcWrite += nodesCurEpoch.at(nodeIdx);
            }
        }
        if(CON::IS_ZERO(userWrite)){
            return 1e6;
        } else{
            return round(((userWrite + gcWrite) / userWrite) * 1e5) / 1e5;
        }
    }

    double UID::getLastGroupWAF(const std::map<uint64_t, uint64_t>& intervalCountTable, uint32_t pagesPerBlock, std::map<uint64_t, uint64_t>::const_iterator& lastGroupItr)
    {
        double ratioOfValidPages = (intervalCountTable.at(CON::UPDATE_INTERVAL_TABLE_SIZE - 1) * CON::TIMETABLE_ENTRY_UNIT) / (groupConf.back() * pagesPerBlock);
        uint64_t numberOfReqs = 0;
        uint64_t numberOfLPAs = 0;
        uint64_t numberOfHotReqs = 0;
        uint64_t numberOfHotLPAs = 0;
        double avgLifeSpan = 0.0;
        double trafficRatioOfHotBlocks = 0.;
        double fractionOfHotBlocks = 0.;
        double WAF = 0.;

        auto tmpItr = lastGroupItr;
        while(tmpItr != intervalCountTable.end()){
            numberOfReqs += tmpItr->first * tmpItr->second;
            numberOfLPAs += tmpItr->second;
            tmpItr++;
        }
        avgLifeSpan = (double)numberOfReqs / (double)numberOfLPAs;

        while(lastGroupItr != intervalCountTable.end()){
            if(lastGroupItr->first < avgLifeSpan * 0.7){
                numberOfHotReqs += lastGroupItr->first * lastGroupItr->second;
                numberOfHotLPAs += lastGroupItr->second;
                lastGroupItr++;
            } else{
                break;
            }
        }

        trafficRatioOfHotBlocks = (double)numberOfHotReqs / (double)numberOfReqs;
        fractionOfHotBlocks = (double)numberOfHotLPAs / (double)numberOfLPAs;

        double incWAF = 1.;
        double testVal = 1.;

        while(testVal > 0){
            testVal = 1 + trafficRatioOfHotBlocks/(exp(trafficRatioOfHotBlocks * ratioOfValidPages)/(fractionOfHotBlocks*incWAF))
                + (1 - trafficRatioOfHotBlocks)/(exp(((1 - trafficRatioOfHotBlocks) * ratioOfValidPages) / ((1 - fractionOfHotBlocks) * incWAF) - 1)) - incWAF;
            incWAF += 1;
        }
        incWAF -= 2;
        testVal = 1.;
        while(testVal > 0){
            testVal = 1 + trafficRatioOfHotBlocks/(exp(trafficRatioOfHotBlocks * ratioOfValidPages)/(fractionOfHotBlocks*incWAF))
                + (1 - trafficRatioOfHotBlocks)/(exp(((1 - trafficRatioOfHotBlocks) * ratioOfValidPages) / ((1 - fractionOfHotBlocks) * incWAF) - 1)) - incWAF;
            incWAF += 0.1;
        }
        incWAF -= 0.2;
        testVal = 1.;
        while(testVal > 0){
            testVal = 1 + trafficRatioOfHotBlocks/(exp(trafficRatioOfHotBlocks * ratioOfValidPages)/(fractionOfHotBlocks*incWAF))
                + (1 - trafficRatioOfHotBlocks)/(exp(((1 - trafficRatioOfHotBlocks) * ratioOfValidPages) / ((1 - fractionOfHotBlocks) * incWAF) - 1)) - incWAF;
            incWAF += 0.01;
        }
        incWAF -= 0.02;
        testVal = 1.;
        while(testVal > 0){
            testVal = 1 + trafficRatioOfHotBlocks/(exp(trafficRatioOfHotBlocks * ratioOfValidPages)/(fractionOfHotBlocks*incWAF))
                + (1 - trafficRatioOfHotBlocks)/(exp(((1 - trafficRatioOfHotBlocks) * ratioOfValidPages) / ((1 - fractionOfHotBlocks) * incWAF) - 1)) - incWAF;
            incWAF += 0.001;
        }
        incWAF -= 0.002;
        testVal = 1.;


        return incWAF;
    }

    Log_Update_Interval::Log_Update_Interval(uint64_t totalBlocksCount, uint32_t pagesPerBlock, const std::vector<uint32_t>& initialGroupConf)
    {
        this->pagesPerBlock = pagesPerBlock;
        requestCountInCurrentInterval = 0;
        currentTimestamp = 1;
        hotFilter = new HotFilter(totalBlocksCount * pagesPerBlock);

        currentUID = NULL;
        changeUIDTag = false;

        currentUID = new UID(initialGroupConf);
        this->totalBlocksCount = totalBlocksCount;

        sumOfUpdateIntervalTable = 0;
        totalReqs = 0;
        totalHotReqs = 0;
        Simulator->lui = this;
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
            double avgBlocksResTime = (double)totalErasedBlocksResidentTime / (double)totalErasedBlocksCount;
            if((totalErasedBlocksCount == 0) || (blkAge < avgBlocksResTime)){
                if(bit != 3){
                    totalHotReqs++;
                    bit++;
                }
            } else if(bit > 0){
                bit--;
            }
        }

        hotFilter->setFilter(lba, bit);
    }

    void Log_Update_Interval::updateTable(const LPA_type lba)
    {
        scheduleCurrentTimestamp();
        setTables(lba);

        if((currentTimestamp % CON::GROUP_CONFIGURE_EPOCH) == 0 && (requestCountInCurrentInterval == 0) && false){
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
            sumOfUpdateIntervalTable += timeInterval;
            totalReqs += 1;
            prevTimestamp = (currentTimestamp << 1);
        }
    }

    void Log_Update_Interval::addBlockAge(const Block_Type* block, const Queue_Type queueType)
    {
        totalErasedBlocksCount++;
        totalErasedBlocksResidentTime += (currentTimestamp - block->createTimestamp);
        if(queueType == Queue_Type::HOT_QUEUE){
        }
        else if(queueType == Queue_Type::LAST_QUEUE){
        }
    }

    void Log_Update_Interval::clearTable()
    {
        updateIntervalTable.clear();
        timestampTable.clear();
        sumOfUpdateIntervalTable = 0;
        totalReqs = 0;
        totalHotReqs = 0;
        totalErasedBlocksCount = 0;
        totalErasedBlocksResidentTime = 0;
    }

    void Log_Update_Interval::selectUID()
    {
        PRINT_MESSAGE("Start group configuration...")
        double avgBlocksResTime = (double)totalErasedBlocksResidentTime / (double)totalErasedBlocksCount;

        PRINT_MESSAGE(avgBlocksResTime);
        if(updateIntervalTable.find(CON::UPDATE_INTERVAL_TABLE_SIZE - 1) == updateIntervalTable.end()){
            updateIntervalTable[CON::UPDATE_INTERVAL_TABLE_SIZE - 1] = 0;
        }
        for(auto& timeTableEntry : timestampTable){
            //first access.
            if((timeTableEntry.second & 1) == 1){
                updateIntervalTable[CON::UPDATE_INTERVAL_TABLE_SIZE - 1]++;
                totalReqs++;
            }
        }

        UID* newUID = new UID();
        double wafForNewUID = newUID->createUID(updateIntervalTable, totalReqs, totalBlocksCount, pagesPerBlock, avgBlocksResTime);
        //TODO.
        double wafForCurUID = currentUID->getWAF(updateIntervalTable, totalReqs, avgBlocksResTime, pagesPerBlock);
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
        if(wafForCurUID > (wafForNewUID - CON::UID_SELECTION_THRESHOLD) || wafForCurUID < 1.0){
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