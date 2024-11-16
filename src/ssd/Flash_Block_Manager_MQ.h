#ifndef FLASH_BLOCK_MANAGER_MQ_H
#define FLASH_BLOCK_MANAGER_MQ_H

#include <set>
#include "Log_Update_Interval.h"
#include "NVM_Transaction_Flash_ER.h"
#include "MQ_types.h"



namespace SSD_Components
{


    enum class MQ_Block_Status {IDLE, WORKING};

    class FTL;

    class Block_Type {
    public:
        NVM::FlashMemory::Physical_Page_Address* blockAddr;

        MQ_Block_Status status = MQ_Block_Status::IDLE;
        lui_timestamp createTimestamp;
        stream_id_type Stream_id = NO_STREAM;
        bool Holds_mapping_data = false;

        flash_page_ID_type writeIdx = 0;
        uint32_t invalid_page_count = 0;
        static uint32_t page_vector_size;
        uint64_t* invalid_page_bitmap = 0;
        int Ongoing_user_read_count = 0;
        int Ongoing_user_program_count = 0;
        
        bool Has_ongoing_gc_wl = false;
        NVM_Transaction_Flash_ER* Erase_transaction = NULL;


        // used by gc to identify previous block.
        level_type prevQueue = UINT32_MAX;

        // used by gc to identify inserted block.
        level_type nextQueue = UINT32_MAX;

        void SetEraseTags(level_type nextQueue);
        bool IsErasing();
        
        void SetUp(level_type prevQueue);
        void StartUsing(lui_timestamp createTimestamp, stream_id_type streamID, bool Holds_mapping_data);

        Block_Type(NVM::FlashMemory::Physical_Page_Address& blockAddr);
        ~Block_Type();
    };

    class Block_Queue{
    public:
        Block_Queue(level_type level);
        ~Block_Queue();
        std::vector<Block_Type*> blockList;
        uint32_t currentBlockIdx;

        std::set<Block_Type*> Ongoing_erase_blocks;
        Block_Type* getCurrentBlock();
        void moveToNextBlock();
        void enqueueBlock(Block_Type* block);
        Block_Type* dequeueBlock();
        level_type getLevel();
        bool isFull();
    private:
        level_type level;
    };


	class Flash_Block_Manager_MQ
	{
	public:
		Flash_Block_Manager_MQ(FTL* ftl, uint32_t channelCount, uint32_t chipsPerChannel, uint32_t diesPerChip, uint32_t planesPerDie, uint32_t blocksPerPlane, uint32_t pagesPerBlock);
		~Flash_Block_Manager_MQ();
	
        void Allocate_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address, uint32_t level);
        void Allocate_mapping_page(const stream_id_type streamID, NVM::FlashMemory::Physical_Page_Address& address);
        
        void Read_transaction_issued(const PPA_type& ppa);
        void Read_transaction_serviced(const PPA_type& ppa);
        void Program_transaction_serviced(const PPA_type& ppa);

        void Invalidate_page_in_block(const stream_id_type streamID, const PPA_type& ppa);
        bool isPageValid(const Block_Type* block, flash_page_ID_type page);

        Block_Type* getBlock(const PPA_type& ppa);

        void finishErase(Block_Type* block);
        bool Stop_servicing_writes(level_type level);

        bool isLastQueue(Block_Queue* queue);

        void startGroupConfiguration(UID* uid);
    private:
        FTL* ftl;
        uint32_t pagesPerBlock;
        std::vector<Block_Queue*> queues;
        std::vector<Block_Type*> blocks;

	    void Program_transaction_issued(Block_Type* block);
	};
}

#endif // !FLASH_BLOCK_MANAGER_MQ_H
