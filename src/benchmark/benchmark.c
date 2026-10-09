#include "benchmark/benchmark.h"

#define HISTOGRAM_STEP_US 1000u
#define PERCENTILE_TARGET 95u
#define PERCENTILE_TOTAL  100u

static void timing_record(BenchmarkTiming* timing, uint32_t us)
{
    timing->sum += us;

    ++timing->count;

    if (us > timing->maximum)
    {
        timing->maximum = us;
    }

    unsigned bin = us ? (us - 1) / HISTOGRAM_STEP_US : 0;

    if (bin >= BENCHMARK_HISTOGRAM_BINS)
    {
        bin = BENCHMARK_HISTOGRAM_BINS - 1;
    }

    ++timing->histogram[bin];
}

void benchmark_record(BenchmarkResult* result, uint32_t elapsed_us, uint32_t preparation, uint32_t update, uint32_t render, uint32_t submit, uint32_t interval)
{
    uint32_t work = preparation + update + render + submit;

    if (!result->frames)
    {
        result->first_work_us = work;
    }

    ++result->frames;

    if (elapsed_us < BENCHMARK_WARMUP_US)
    {
        ++result->warmup_frames;
        return;
    }

    result->over_budget += work > BENCHMARK_BUDGET_US;

    timing_record(&result->preparation, preparation);
    timing_record(&result->update, update);
    timing_record(&result->render, render);
    timing_record(&result->submit, submit);
    timing_record(&result->work, work);
    timing_record(&result->interval, interval);
}

double benchmark_mean(const BenchmarkTiming* timing)
{
    return timing->count ? (double)timing->sum / timing->count : 0;
}

uint32_t benchmark_p95(const BenchmarkTiming* timing)
{
    unsigned seen   = 0;
    unsigned target = (timing->count * PERCENTILE_TARGET + PERCENTILE_TOTAL - 1) / PERCENTILE_TOTAL;

    if (!target)
    {
        return 0;
    }

    for (unsigned i = 0; i < BENCHMARK_HISTOGRAM_BINS; ++i)
    {
        seen += timing->histogram[i];

        if (seen >= target)
        {
            return i == BENCHMARK_HISTOGRAM_BINS - 1 ? timing->maximum : (i + 1) * HISTOGRAM_STEP_US;
        }
    }

    return timing->maximum;
}
