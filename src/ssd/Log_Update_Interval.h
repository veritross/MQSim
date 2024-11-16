#ifndef LOG_UPDATE_INTERVAL_MQ_H
#define LOG_UPDATE_INTERVAL_MQ_H

#include <vector>
#include "MQ_types.h"


namespace SSD_Components
{
	class Block_Type;


	class CON{
	private:
	public:
		const static uint32_t ENTRY_UNIT;
		const static bool ENTRY_VERIFY(LPA_type lba){
			return lba % ENTRY_UNIT == 0;
		}
		const static uint32_t REQS_PER_INTERVAL;
		const static lui_timestamp TIMESTAMP_NOT_ACCESSED;
		const static uint8_t HOT_FILTER_BITS_COUNT;

		const static uint8_t NOT_NOTICIBLE_REDUCTION_THRESHOLD;
		const static double NOTICIBLE_REDUCTION_CRITERIA;
		const static double UID_SELECTION_THRESHOLD;

		const static lui_timestamp SELECT_EPOCH_TIMESTAMP;
		const static bool IS_ZERO(double v){
			return v < 1e-9;
		}

	};

	struct HotFilter{
		uint64_t* filter;
		uint32_t totalBlocksCount;
		uint64_t totalBlocksAge;

		HotFilter(uint64_t noOfBlocks);
		~HotFilter();
	};

	class UID{
	private:
		//key. group count.
		//value. group size.
		static const double MarkovChain(const std::vector<double>& p, double lastBlocksAvgValidPagesRatio);
	public:
		std::vector<uint32_t> groupConf;
		double createUID(const std::vector<uint64_t>& intervalCountTable, uint64_t totalReqs, uint64_t avgResTime, double lastBlocksAvgValidPagesRatio);
		double getWAF(const std::vector<uint64_t>& intervalCountTable, uint64_t totalReqs, uint64_t avgResTime, double lastBlocksAvgValidPagesRatio);
	};


	class Log_Update_Interval 
	{
	private:
		uint32_t pagesPerBlock;
		lui_timestamp requestCountInCurrentInterval;
		lui_timestamp currentTimestamp;
		uint64_t lastBlocksCount;
		uint64_t lastBlocksValidPagesCount;


		HotFilter* hotFilter;
		void addHotFilter(const LPA_type lba);

		std::vector<uint64_t> intervalCountTable;
		std::vector<lui_timestamp> timestampTable;
		lui_timestamp& getTimestampEntry(const LPA_type lba);

		void scheduleCurrentTimestamp();
		void updateTimestamp(const LPA_type lba);

		UID* currentUID;
		bool changeUIDTag;
		void selectUID();

	public:
		Log_Update_Interval(uint64_t noOfBlocks, uint32_t pagesPerBlock);
        ~Log_Update_Interval();

        bool isHot(const LPA_type lba);
		void writeData(const LPA_type lba);
		void addBlockAge(const Block_Type* block, const Queue_Type queueType);

		UID* getUID();
		lui_timestamp getCurrentTimestamp();
	};
}

#endif // !LOG_UPDATE_INTERVAL_MQ_H
