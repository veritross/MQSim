#include "Flash_Block_Manager_MQ.h"
#include <queue>
#include "MQ_GC_Unit.h"
#include "FTL.h"


namespace SSD_Components{
    uint32_t Block_Type::page_vector_size = 0;
    void Block_Type::SetEraseTags(level_type nextQueue)
    {
        if(this->nextQueue != UINT32_MAX || Has_ongoing_gc_wl || Erase_transaction != NULL){
            PRINT_ERROR("Set erase tags")
        }
        this->nextQueue = nextQueue;
        this->Has_ongoing_gc_wl = true;
    }

    bool Block_Type::IsErasing()
    {
        return Has_ongoing_gc_wl;
    }

    void Block_Type::SetUp(level_type prevQueue)
    {
        status = MQ_Block_Status::IDLE;
        createTimestamp = 0;
        Stream_id = NO_STREAM;
        Holds_mapping_data = false;
        
        writeIdx = 0;
        invalid_page_count = 0;
        for (unsigned int i = 0; i < page_vector_size; i++) {
			invalid_page_bitmap[i] = MQ_All_VALID_PAGE;
		}
        Ongoing_user_read_count = 0;
        Ongoing_user_program_count = 0;
        
        Has_ongoing_gc_wl = false;
        Erase_transaction = NULL;

        this->prevQueue = prevQueue;
        nextQueue = UINT32_MAX;
    }

    void Block_Type::StartUsing(lui_timestamp createTimestamp, stream_id_type streamID, bool Holds_mapping_data)
    {
        this->createTimestamp = createTimestamp;
        this->Stream_id = streamID;
        this->Holds_mapping_data = Holds_mapping_data;
        this->status = MQ_Block_Status::WORKING;
    }

    Block_Type::Block_Type(NVM::FlashMemory::Physical_Page_Address &blockAddr)
    {
        this->blockAddr = new NVM::FlashMemory::Physical_Page_Address(blockAddr);
    }

    Block_Type::~Block_Type()
    {
        delete this->blockAddr;
    }

    Block_Queue::Block_Queue(level_type level)
    {
        this->level = level;
        currentBlockIdx = 0;
    }

    Block_Queue::~Block_Queue()
    {
    }

    Block_Type *Block_Queue::getCurrentBlock()
    {
        return blockList.at(currentBlockIdx);
    }

    void Block_Queue::moveToNextBlock()
    {
        currentBlockIdx++;
    }

    void Block_Queue::enqueueBlock(Block_Type *block)
    {
        block->SetUp(this->level);
        blockList.push_back(block);
    }

    Block_Type *Block_Queue::dequeueBlock()
    {
        if(blockList.size() < MIN_QUEUE_SIZE){
            PRINT_ERROR("Min queue size has been reached")
        }
        
        auto targetBlock = blockList.begin();
        blockList.erase(targetBlock);
        currentBlockIdx--;
        return *targetBlock;
    }

    
    level_type Block_Queue::getLevel()
    {
        return this->level;
    }

    bool Block_Queue::isFull()
    {
        return currentBlockIdx == (blockList.size() - 1);
    }

    Block_Type *Flash_Block_Manager_MQ::getBlock(const PPA_type &ppa)
    {
        uint32_t blockID = ppa / pagesPerBlock;
        return blocks[blockID];
    }

    void Flash_Block_Manager_MQ::finishErase(Block_Type *block)
    {
        Block_Queue* prevQueue = queues.at(block->prevQueue);
        Block_Queue* nextQueue = queues.at(block->nextQueue);
        nextQueue->enqueueBlock(block);
    }

    // When the level has no free space for writes.
    bool Flash_Block_Manager_MQ::Stop_servicing_writes(level_type level)
    {
        return queues[level]->isFull() && (queues[level]->getCurrentBlock()->writeIdx == pagesPerBlock);
    }

    bool Flash_Block_Manager_MQ::isLastQueue(Block_Queue *queue)
    {
        return (queue->getLevel() == queues.size() - 1);
    }

    void Flash_Block_Manager_MQ::startGroupConfiguration(UID *uid)
    {
        std::queue<Block_Type*> blockPool;
        std::queue<std::pair<level_type, uint32_t>> queuesNeedBlocks;
        for(uint32_t groupNumber = 1; groupNumber < uid->groupConf.size(); groupNumber++){
            Block_Queue* currentQueue = queues.at(groupNumber);
            Block_Queue* nextQueue = queues.at(groupNumber + 1);

            int blocksToInsert = uid->groupConf.at(groupNumber) - currentQueue->blockList.size();
            if(blocksToInsert < 0){
                for(uint32_t blockCount = 0; blockCount < (uint32_t)(-1 * blocksToInsert); blockCount++){
                    Block_Type* lastBlock = currentQueue->blockList.back();
                    if(lastBlock->status == MQ_Block_Status::IDLE){
                        blockPool.push(lastBlock);
                        currentQueue->blockList.pop_back();
                    } else{
                        blockPool.push(currentQueue->blockList.front());
                        currentQueue->blockList.erase(currentQueue->blockList.begin());
                    }
                }
            } else if(blocksToInsert > 0){
                queuesNeedBlocks.push({groupNumber, blocksToInsert});
            }
        }

        while(!queuesNeedBlocks.empty()){
            std::pair<level_type, uint32_t>& q = queuesNeedBlocks.front();
            for(uint32_t blockCount = 0; blockCount < q.second; blockCount++){
                Block_Type* blockToInsert = blockPool.front();
                if(blockToInsert->status == MQ_Block_Status::IDLE){
                    queues.at(q.first)->enqueueBlock(blockToInsert);
                } else{
                    ftl->GC_and_WL_Unit->gc_start(queues.at(blockToInsert->prevQueue), queues.at(q.first));
                }
                blockPool.pop();
            }
            queuesNeedBlocks.pop();
        }

        if(blockPool.size() != 0){
            PRINT_ERROR("Group configuration.")
        }
    }

    Flash_Block_Manager_MQ::Flash_Block_Manager_MQ(FTL *ftl, uint32_t channelCount, uint32_t chipsPerChannel, uint32_t diesPerChip, uint32_t planesPerDie, uint32_t blocksPerPlane, uint32_t pagesPerBlock)
    :ftl(ftl), pagesPerBlock(pagesPerBlock) {
        //In initialize, we assumed that MiDAS has only hot queue.
        queues.resize(1);
        Block_Pool_Slot_Type::Page_vector_size = pagesPerBlock / (sizeof(uint64_t) * 8) + (pagesPerBlock % (sizeof(uint64_t) * 8) == 0 ? 0 : 1);

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

    void Flash_Block_Manager_MQ::Allocate_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address &address, uint32_t level)
    {
        Block_Queue* queue = queues.at(level);

        Block_Type* block;
        block = queue->getCurrentBlock();
        Program_transaction_issued(block);
        address = *block->blockAddr;
        address.PageID = block->writeIdx++;

        if(block->writeIdx == pagesPerBlock){
            queue->moveToNextBlock();
            if(queue->isFull()){
                if(isLastQueue(queue)){
                    ftl->GC_and_WL_Unit->gc_start(queue, queue);
                } else{
                    ftl->GC_and_WL_Unit->gc_start(queue, queues.at(level + 1));
                }
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
        getBlock(ppa)->invalid_page_bitmap[pageID / 64] &= ~(1 << pageID % 64);
        getBlock(ppa)->invalid_page_count++;
    }

    bool Flash_Block_Manager_MQ::isPageValid(const Block_Type *block, flash_page_ID_type page)
    {
        return (block->invalid_page_bitmap[page / 64] & (1 << page % 64));
    }

}