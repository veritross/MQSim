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
		// In pages unit.
		const static lui_timestamp GROUP_CONFIGURE_EPOCH;
		const static bool IS_ZERO(double v){
			return (v < 1e-9 && v > -1e-9);
		}
		const static uint32_t TIMETABLE_ENTRY_UNIT;
		const static bool ENTRY_VERIFY(LPA_type lba){
			return (lba % TIMETABLE_ENTRY_UNIT) == 0;
		}
		const static lui_timestamp TIMESTAMP_NOT_ACCESSED;
		const static uint64_t UPDATE_INTERVAL_TABLE_SIZE;
		const static uint8_t HOT_FILTER_BITS_COUNT;

		const static uint8_t NOT_NOTICIBLE_REDUCTION_THRESHOLD;
		const static double NOTICIBLE_REDUCTION_CRITERIA;
		const static double UID_SELECTION_THRESHOLD;

	};

	struct HotFilter{
		uint64_t* filter;

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

		uint64_t totalHotBlocksAge;
		uint64_t totalErasedHotBlocksCount;
		uint64_t totalErasedLastBlocksCount;
		uint64_t totalErasedLastBlocksValidPagesCount;

		//TODO. clear hot filter.
		HotFilter* hotFilter;

		//keeps track of the number of pages for specific update intervals.
		// Sampling rate is 0.01(one in every 100 blocks)
		std::vector<uint64_t> updateIntervalTable;

		//records timestamps of page updates to compute the update intervals of data pages.
		std::vector<lui_timestamp> timestampTable;

		void scheduleCurrentTimestamp();
		void setTables(const LPA_type lba);

		UID* currentUID;
		bool changeUIDTag;
		void selectUID();

	public:
		Log_Update_Interval(uint64_t noOfBlocks, uint32_t pagesPerBlock);
        ~Log_Update_Interval();

        bool isHot(const LPA_type lba);
		void updateHotFilter(const LPA_type lba, const lui_timestamp blkAge, const level_type level, const bool forGC);
		void updateTable(const LPA_type lba);
		void addBlockAge(const Block_Type* block, const Queue_Type queueType);

		UID* getUID();
		lui_timestamp getCurrentTimestamp();
	};
}

#endif // !LOG_UPDATE_INTERVAL_MQ_H
