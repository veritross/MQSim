#ifndef GC_UNIT_MQ_H
#define GC_UNIT_MQ_H

#include "Engine.h"
#include "Flash_Chip.h"

namespace SSD_Components
{
    class FTL;
    class Block_Type;
    class Block_Queue;
    class NVM_Transaction_Flash;

    class MQ_GC_Unit: public MQSimEngine::Sim_Object {
    private:
        static MQ_GC_Unit* _my_instance;
        FTL* ftl;

        uint32_t pagesPerBlock;
        uint32_t sectorsPerPage;

        static void handle_transaction_serviced_signal_from_PHY(NVM_Transaction_Flash* transaction);
        Block_Type* selectVictimBlockFront(Block_Queue* queue);
        Block_Type* selectVictimBlockCB(Block_Queue *queue, lui_timestamp currentTimeStamp);
        bool isValidForVictimBlock(const Block_Type* block);
    public:
        MQ_GC_Unit(const sim_object_id_type &id, FTL* ftl, uint32_t pagesPerBlock, uint32_t sectorsPerPage);
        ~MQ_GC_Unit();

        void Setup_triggers();
		void Start_simulation();
		void Validate_simulation_config();
		void Execute_simulator_event(MQSimEngine::Sim_Event*);
        bool gc_start(Block_Queue* prevQueue, lui_timestamp currentTimeStamp);
	    bool GC_is_in_urgent_mode(NVM::FlashMemory::Flash_Chip* const chip);

        void submitTransactions(Block_Type* victimBlock);
    };
}

#endif //GC_UNIT_MQ_H