#include "Flash_Block_Manager_MQ.h"
#include <queue>
#include "MQ_GC_Unit.h"
#include "FTL.h"


namespace SSD_Components{
    uint32_t Block_Type::page_vector_size = 0;
    Block_Type::Block_Type(NVM::FlashMemory::Physical_Page_Address &blockAddr)
    {
        this->blockAddr = new NVM::FlashMemory::Physical_Page_Address(blockAddr);
    }

    Block_Type::~Block_Type()
    {
        delete this->blockAddr;
    }

    void Block_Type::SetUp(level_type prevQueue)
    {
        status = MQ_Block_Status::IDLE;
        createTimestamp = 0;
        Stream_id = NO_STREAM;
        Holds_mapping_data = false;
        
        currentPageIdx = 0;
        invalid_page_count = 0;
        for (unsigned int i = 0; i < page_vector_size; i++) {
			invalid_page_bitmap[i] = MQ_All_VALID_PAGE;
		}
        Ongoing_user_read_count = 0;
        Ongoing_user_program_count = 0;
        
        Erase_transaction = nullptr;

        prevQueue = prevQueue;
        nextQueue = UNDEFINED_LEVEL;
    }

    void Block_Type::StartUsing(lui_timestamp createTimestamp, stream_id_type streamID, bool Holds_mapping_data)
    {
        this->status = MQ_Block_Status::WORKING;
        this->createTimestamp = createTimestamp;
        this->Stream_id = streamID;
        this->Holds_mapping_data = Holds_mapping_data;
    }

    void Block_Type::SetEraseTags(level_type nextQueue)
    {
        if(this->nextQueue != UNDEFINED_LEVEL || this->status == MQ_Block_Status::ERASING || Erase_transaction != nullptr){
            PRINT_ERROR("Set erase tags")
        }
        this->status = MQ_Block_Status::ERASING;
        this->nextQueue = nextQueue;
    }

    Block_Queue::Block_Queue(level_type level)
    {
        this->level = level;
        currentBlockIdx = 0;
        blockList.clear();
    }

    Block_Queue::~Block_Queue()
    {
    }

    Block_Type *Block_Queue::getCurrentBlock()
    {
        return blockList.at(currentBlockIdx);
    }
    
    void Block_Queue::enqueueBlock(Block_Type *block)
    {
        block->SetUp(this->level);
        blockList.push_back(block);
    }
    
    level_type Block_Queue::getLevel()
    {
        return this->level;
    }

    bool Block_Queue::isFull()
    {
        return currentBlockIdx == blockList.size();
    }

    Block_Type *Flash_Block_Manager_MQ::getBlock(const PPA_type &ppa)
    {
        uint32_t blockID = ppa / pagesPerBlock;
        return blocks[blockID];
    }

    void Flash_Block_Manager_MQ::finishErase(Block_Type *block)
    {
        Block_Queue* queue = queues.at(block->prevQueue);
        block->SetUp(block->prevQueue);
        auto blockItr = queue->blockList.begin();
        for(uint32_t blockIdx = 0; blockItr != queue->blockList.end(); blockItr++, blockIdx++){
            if((*blockItr) == block){
                queue->blockList.erase(blockItr);
                queue->blockList.push_back(block);
                if(queue->currentBlockIdx > blockIdx){
                    queue->currentBlockIdx--;
                }
                return;
            }
        }

        PRINT_ERROR("FINISH ERASE")
    }

    // When the level has no free space for writes.
    bool Flash_Block_Manager_MQ::Stop_servicing_writes(const level_type level)
    {
        return queues.at(level)->isFull();
    }

    bool Flash_Block_Manager_MQ::isLastQueue(level_type level)
    {
        return level == (queues.size() - 1);
    }

    void Flash_Block_Manager_MQ::startGroupConfiguration()
    {
        UID* uid = lui->getUID();
        if(uid == nullptr) return;
        std::queue<Block_Type*> blockPool;

        while(queues.size() < uid->groupConf.size()){
            createQueue();
        }

        // Pop the blocks from queues.
        for(uint32_t groupNumber = 1; groupNumber < queues.size(); groupNumber++){
            Block_Queue* currentQueue = queues.at(groupNumber);
            int popCount = (currentQueue->blockList.size() - uid->groupConf.at(groupNumber));
            while(popCount > 0){
                Block_Type* blockToPop = currentQueue->blockList.back();

                if(blockToPop->status == MQ_Block_Status::IDLE){
                    // If there is a block is not used, just pop.
                    blockPool.push(blockToPop);
                    currentQueue->blockList.pop_back();
                } else{
                    // Else, pop the head block.
                    blockPool.push(currentQueue->blockList.front());
                    currentQueue->blockList.erase(currentQueue->blockList.begin());
                }
                popCount--;
            }
        }

        // Push the blocks to queues.
        for(uint32_t groupNumber = 1; groupNumber < queues.size(); groupNumber++){
            Block_Queue* currentQueue = queues.at(groupNumber);
            int pushCount = uid->groupConf.at(groupNumber) - currentQueue->blockList.size();
            while(pushCount > 0){
                Block_Type* insertBlock = blockPool.front(); blockPool.pop();

                if(insertBlock->status == MQ_Block_Status::WORKING){
                    level_type nextLevel = insertBlock->prevQueue + 1;
                    if(insertBlock->prevQueue == uid->groupConf.size() - 1){
                        nextLevel = insertBlock->prevQueue;
                    }
                    ftl->GC_and_WL_Unit->gc_start_specified_block(insertBlock, nextLevel);
                } else if(insertBlock->status == MQ_Block_Status::ERASING){
                    insertBlock->prevQueue = groupNumber;
                }
                currentQueue->enqueueBlock(insertBlock);
                pushCount--;
            }
        }

        while(queues.size() > uid->groupConf.size()){
            removeLastQueue();
        }

        for(uint32_t groupNumber = 1; groupNumber < queues.size(); groupNumber++){
            Block_Queue* curQueue = queues.at(groupNumber);
            if(curQueue->isFull()){
                ftl->GC_and_WL_Unit->gc_start(curQueue, lui->getCurrentTimestamp());
            }
        }
    }

    void Flash_Block_Manager_MQ::handleHotFilter(const LPA_type& lpa, const PPA_type& old_ppa, const bool forGC)
    {
        Block_Type* block = getBlock(old_ppa);
        lui_timestamp blkAge = (lui->getCurrentTimestamp() - block->createTimestamp);

        lui->updateHotFilter(lpa, blkAge, block->prevQueue, forGC);
    }

    bool Flash_Block_Manager_MQ::isHot(const LPA_type &lpa)
    {
        return lui->isHot(lpa);
    }

    void Flash_Block_Manager_MQ::handleLUIBlockAge(Block_Type *block)
    {
        if(isLastQueue(block->prevQueue)){
            lui->addBlockAge(block, Queue_Type::LAST_QUEUE);
        } else{
            if(block->prevQueue == 0){
                lui->addBlockAge(block, Queue_Type::HOT_QUEUE);
            }
        }
    }

    Flash_Block_Manager_MQ::Flash_Block_Manager_MQ(FTL *ftl, uint32_t channelCount, uint32_t chipsPerChannel, uint32_t diesPerChip, uint32_t planesPerDie, uint32_t blocksPerPlane, uint32_t pagesPerBlock)
    :ftl(ftl), pagesPerBlock(pagesPerBlock) {
        //In initialize, we assumed that MiDAS has only hot queue.
        Block_Type::page_vector_size = pagesPerBlock / (sizeof(uint64_t) * 8) + (pagesPerBlock % (sizeof(uint64_t) * 8) == 0 ? 0 : 1);

        uint64_t totalBlockCount = channelCount * chipsPerChannel * diesPerChip * planesPerDie * blocksPerPlane;
        createQueue();
        
        for(uint32_t channelID = 0; channelID < channelCount; channelID++){
            for(uint32_t chipID = 0; chipID < chipsPerChannel; chipID++){
                for(uint32_t dieID = 0; dieID < diesPerChip; dieID++){
                    for(uint32_t planeID = 0; planeID < planesPerDie; planeID++){
                        for(uint32_t blockID = 0; blockID < blocksPerPlane; blockID++){
                            NVM::FlashMemory::Physical_Page_Address blockAddr = NVM::FlashMemory::Physical_Page_Address(channelID, chipID, dieID, planeID, blockID, 0);
                            Block_Type* newBlock = new Block_Type(blockAddr);
                            blocks.push_back(newBlock);
                            queues.at(0)->enqueueBlock(newBlock);
                        }
                    }
                }
            }
        }
    }

    Flash_Block_Manager_MQ::~Flash_Block_Manager_MQ()
    {
        for(auto block : blocks){
            delete block;
        }
    }

    void Flash_Block_Manager_MQ::Allocate_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address &address, LPA_type lpa, uint32_t& level)
    {
        if(queues.size() < level - 1){
            level = queues.size() - 1;
        }

		lui->updateTable(lpa);
        startGroupConfiguration();

        Block_Queue* queue = queues.at(level);
        Block_Type* block = queue->getCurrentBlock();
        Program_transaction_issued(block);
        address = *block->blockAddr;
        address.PageID = block->currentPageIdx++;

        if(block->currentPageIdx == pagesPerBlock){
            queue->currentBlockIdx++;
            while(!(queue->isFull())){
                block = queue->getCurrentBlock();
                if(block->status == MQ_Block_Status::IDLE) break;
                else queue->currentBlockIdx++;
            }
            if(queue->isFull()){
                ftl->GC_and_WL_Unit->gc_start(queue, lui->getCurrentTimestamp());
            } else{
                block->StartUsing(lui->getCurrentTimestamp(), streamID, false);
            }
        }
    }

    void Flash_Block_Manager_MQ::Allocate_mapping_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address &address)
    {
        PRINT_ERROR("NOT IMPLEMENTED ALLOCATE MAPPING PAGE")
    }

    void Flash_Block_Manager_MQ::Program_transaction_issued(Block_Type *block)
    {
        block->Ongoing_user_program_count++;
    }

    void Flash_Block_Manager_MQ::createQueue()
    {
        Block_Queue* queue = new Block_Queue(this->queues.size());
        this->queues.push_back(queue);
    }

    void Flash_Block_Manager_MQ::removeLastQueue()
    {
        Block_Queue* lastQueue = queues.back();

        if(lastQueue->blockList.size() != 0){
            PRINT_ERROR("Remove Last Queue")
        }

        queues.pop_back();
    }

    void Flash_Block_Manager_MQ::Read_transaction_issued(const PPA_type& ppa)
    {
        getBlock(ppa)->Ongoing_user_read_count++;
    }

    void Flash_Block_Manager_MQ::Read_transaction_serviced(const PPA_type &ppa)
    {
        getBlock(ppa)->Ongoing_user_read_count--;
    }

    void Flash_Block_Manager_MQ::Program_transaction_serviced(const PPA_type &ppa)
    {
        getBlock(ppa)->Ongoing_user_program_count--;
    }

    void Flash_Block_Manager_MQ::Invalidate_page_in_block(const stream_id_type streamID, const PPA_type &ppa)
    {
        uint32_t pageID = ppa % pagesPerBlock;
        Block_Type* block = getBlock(ppa);
        
        block->invalid_page_bitmap[pageID / 64] &= ~(1 << (pageID % 64));
        block->invalid_page_count++;
    }

    bool Flash_Block_Manager_MQ::isPageValid(const Block_Type *block, flash_page_ID_type page)
    {
        return (block->invalid_page_bitmap[page / 64] & (1 << (page % 64)));
    }

}