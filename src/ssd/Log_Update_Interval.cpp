#include "Log_Update_Interval.h"
#include "Sim_Defs.h"
#include "Flash_Block_Manager_MQ.h"

namespace SSD_Components{

    const uint32_t CON::TIMETABLE_ENTRY_UNIT = 100;
    const lui_timestamp CON::TIMESTAMP_NOT_ACCESSED = UINT64_MAX;
    const uint8_t CON::HOT_FILTER_BITS_COUNT = 2;

    const uint64_t CON::UPDATE_INTERVAL_TABLE_SIZE = 1e4;
    const lui_timestamp CON::GROUP_CONFIGURE_EPOCH = 1e4;

    const double CON::NOTICIBLE_REDUCTION_CRITERIA = 0.005;
    const uint8_t CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD = 5;

    const double CON::UID_SELECTION_THRESHOLD = 0.005;

    HotFilter::HotFilter(uint64_t noOfPages)
    {
        filter.resize(noOfPages, 0);
    }

    HotFilter::~HotFilter()
    {
    }

    void HotFilter::clearFilter()
    {
        std::fill(filter.begin(), filter.end(), 0);
    }

    UID::UID(){
        groupConf = std::vector<uint32_t>();
    }

    UID::UID(const std::vector<uint32_t> &groupConf)
    :groupConf(groupConf) {}


    double UID::createUID(const std::vector<uint64_t> &intervalCountTable, uint64_t totalReqs, uint32_t totalBlocksCount, uint32_t pagesPerBlock)
    {
        if(this->groupConf.size() > 0){
            PRINT_ERROR("Create UID : UID is already charged. size - " << this->groupConf.size())
        }
        auto hotSplit = [&]() -> double {
           
            uint32_t hotBlocksCount = (intervalCountTable[0] / pagesPerBlock) + 1;
            this->groupConf.push_back(hotBlocksCount);
            this->groupConf.push_back(totalBlocksCount - hotBlocksCount);
            uint32_t prevGroupIdx = 0;
            uint32_t nextGroupIdx = 1;

            uint32_t optPrevGroupCount = hotBlocksCount;
            double optWAF = 10000.0;

            double prevWAF = 10000.0;

            while(true){
                prevWAF = this->getWAF(intervalCountTable, totalReqs);
                if(prevWAF < optWAF){
                    optWAF = prevWAF;
                    optPrevGroupCount = this->groupConf.at(prevGroupIdx);
                } else if(!CON::IS_ZERO(optWAF - prevWAF)){
                    break;
                }

                if(this->groupConf.at(nextGroupIdx) == MIN_QUEUE_SIZE){
                    break;
                }
                this->groupConf.at(prevGroupIdx)++;
                this->groupConf.at(nextGroupIdx)--;
            }

            if(CON::IS_ZERO(optWAF - 10000.0)){
                return optWAF;
            } else{
                uint32_t curBlocksCount = this->groupConf.at(prevGroupIdx) + this->groupConf.at(nextGroupIdx);
                this->groupConf.at(prevGroupIdx) = optPrevGroupCount;
                this->groupConf.at(nextGroupIdx) = curBlocksCount - optPrevGroupCount;
                return optWAF;
            }
        };

        auto split = [&]() -> double {
            uint32_t prevGroupIdx = this->groupConf.size() - 1;
            uint32_t nextGroupIdx = this->groupConf.size();
            this->groupConf.push_back(this->groupConf.at(prevGroupIdx) - MIN_QUEUE_SIZE);
            this->groupConf.at(prevGroupIdx) = MIN_QUEUE_SIZE;

            uint32_t optPrevGroupCount = this->groupConf.at(prevGroupIdx);
            double optWAF = 10000.0;

            double prevWAF = 10000.0;

            while(true){
                prevWAF = this->getWAF(intervalCountTable, totalReqs);
                if(prevWAF < optWAF){
                    optWAF = prevWAF;
                    optPrevGroupCount = this->groupConf.at(prevGroupIdx);
                } else if(!CON::IS_ZERO(optWAF - prevWAF)){
                    break;
                }

                if(this->groupConf.at(nextGroupIdx) == MIN_QUEUE_SIZE){
                    break;
                }
                this->groupConf.at(prevGroupIdx)++;
                this->groupConf.at(nextGroupIdx)--;
            }

            if(CON::IS_ZERO(optWAF - 10000.0)){
                return optWAF;
            } else{
                uint32_t curBlocksCount = this->groupConf.at(prevGroupIdx) + this->groupConf.at(nextGroupIdx);
                this->groupConf.at(prevGroupIdx) = optPrevGroupCount;
                this->groupConf.at(nextGroupIdx) = curBlocksCount - optPrevGroupCount;
                return optWAF;
            }
        };

        double optWAF = hotSplit();
        std::vector<uint32_t> optGroupConf = this->groupConf;

        double prevWAF = 0.0;
        uint8_t notNoticibleReductionCount = 0;

        while(true){
            prevWAF = split();
            if(prevWAF < optWAF){
                if(prevWAF < optWAF * (1.0 - CON::NOTICIBLE_REDUCTION_CRITERIA)){
                    optWAF = prevWAF;
                    optGroupConf = this->groupConf;
                    notNoticibleReductionCount = 0;
                } else{
                    notNoticibleReductionCount++;
                }

                if(notNoticibleReductionCount == CON::NOT_NOTICIBLE_REDUCTION_THRESHOLD){
                    break;
                }
            } else if(!CON::IS_ZERO(optWAF - prevWAF)){
                break;
            }
        }

        this->groupConf = optGroupConf;
        return optWAF;
    }
    
    double UID::getWAF(const std::vector<uint64_t>& intervalCountTable, uint64_t totalReqs)
    {
        // 4.4 Estimating Transition Probabilities.
        std::vector<double> p = std::vector<double>(groupConf.size(), 0.0);
        std::vector<uint32_t> waitingPeriod = std::vector<uint32_t>(groupConf.size(), 0);

        uint32_t intervalCountTableIdx = 0;

        double sumOfP = 0.0;
        waitingPeriod.at(0) = groupConf.at(0) / (1.0 - sumOfP);
        for(; intervalCountTableIdx < waitingPeriod.at(0); intervalCountTableIdx++){
            p.at(0) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
        }
        sumOfP += p.at(0);

        for(int i = 1; i < groupConf.size(); i++){
            waitingPeriod.at(i) = (groupConf.at(i) / (1.0 - sumOfP)) + waitingPeriod.at(i - 1);
            for(; intervalCountTableIdx < waitingPeriod.at(i) && intervalCountTableIdx < intervalCountTable.size() - 1; intervalCountTableIdx++){
                p.at(i) += ((double)intervalCountTable.at(intervalCountTableIdx) / (double)totalReqs);
            }
            sumOfP += p.at(i);
        }

        double tmp = 0.0;
        for(; intervalCountTableIdx < intervalCountTable.size(); intervalCountTableIdx++){
            tmp += (double)intervalCountTable.at(intervalCountTableIdx) / (double(totalReqs));
        }
        
        return MarkovChain(p, (tmp / (p.back() + tmp)), ((double)intervalCountTable.at(0) / (double)totalReqs));
    }

    double UID::MarkovChain(const std::vector<double>& p, double lastBlocksAvgValidPagesRatio, double hotTrafficRatio)
    {
        // 4.3. Prediction of WAF using MCAM.

        //1. G(n) -> G(n + 1).
        //2. G(n) -> Free.
        std::vector<std::pair<double, double>> transitionProb;
        transitionProb.resize(p.size());

        //1. Free -> G(hot).
        //2. Free -> G(1).
        std::pair<double, double> freeNode = {hotTrafficRatio, 1.0 - hotTrafficRatio};

        transitionProb.front() = {1.0 - p.at(0), p.at(0)};

        double sumOfP = p.back();
        for(int i = transitionProb.size() - 2; i > 0; i--){
            transitionProb.at(i).first = sumOfP / (sumOfP + p.at(i));
            transitionProb.at(i).second = 1.0 - transitionProb.at(i).first;
        }
        transitionProb.back() = {lastBlocksAvgValidPagesRatio, 1.0 - lastBlocksAvgValidPagesRatio};

        std::vector<double> nodesCurEpoch;
        std::vector<double> nodesNextEpoch;
        nodesCurEpoch.resize(p.size(), 0.0);
        nodesNextEpoch.resize(p.size(), 0.0);

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

        // Hot and G1.
        if(groupConf.size() == 2){
            return ((nodesCurEpoch.at(1) + (freeNodeCurEpoch * transitionProb.at(0).first))/ freeNodeCurEpoch);
        }
        // Else.
        else{
            double userWrite = (nodesCurEpoch.at(0) + nodesCurEpoch.at(1));
            double gcWrite = 0.0;
            for(uint32_t groupIdx = 2; groupIdx < nodesCurEpoch.size(); groupIdx++){
                gcWrite += nodesCurEpoch.at(groupIdx);
            }
            gcWrite += nodesCurEpoch.at(0) * transitionProb.at(0).first;
            return ((userWrite + gcWrite) / userWrite);
        }
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

        updateIntervalTable.resize(CON::UPDATE_INTERVAL_TABLE_SIZE);
        timestampTable.resize((totalBlocksCount * pagesPerBlock) / CON::TIMETABLE_ENTRY_UNIT, CON::TIMESTAMP_NOT_ACCESSED);

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
        return (hotFilter->filter.at(lba) == 3);
    }

    void Log_Update_Interval::updateHotFilter(const LPA_type lba, const lui_timestamp blkAge, const level_type level, const bool forGC)
    {
        uint8_t& bit = hotFilter->filter.at(lba);

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
        std::fill(updateIntervalTable.begin(), updateIntervalTable.end(), 0);
        std::fill(timestampTable.begin(), timestampTable.end(), CON::TIMESTAMP_NOT_ACCESSED);
    }

    void Log_Update_Interval::selectUID()
    {
        uint64_t totalReqs = 0;
        for(auto updateIntervalTableEntry : updateIntervalTable){
            totalReqs += updateIntervalTableEntry;
        }

        for(auto& timeTableEntry : timestampTable){
            //first access.
            if(timeTableEntry != CON::TIMESTAMP_NOT_ACCESSED && (timeTableEntry & 1) == 1){
                updateIntervalTable.back()++;
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
        if(true){
        //if(wafForCurUID > (wafForNewUID * (1.0 - CON::UID_SELECTION_THRESHOLD))){
            delete currentUID;
            currentUID = newUID;
            changeUIDTag = true;
        } else{
            delete newUID;
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