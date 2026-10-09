#include "ui/visualizer/scene_batch.h"
#include "unity.h"
#include <stdio.h>

/**
 * @brief Run regression checks for GS batch packet encoding.
 *
 * Decode every GS write in order for lines, sprites and flat/shaded textured
 * triangles. Verify their distinct color/UV/position layouts, odd payload
 * padding and the untouched word immediately after each packet.
 *
 */
static void packet_regressions(void)
{
    uint64_t vertices[1152], packet[1160];

    for (unsigned registers = 3; registers <= 9; ++registers)
    {
        if (registers != 3 && registers != 4 && registers != 7 && registers != 9)
        {
            continue;
        }

        for (unsigned count = 1; count <= 128; ++count)
        {
            for (unsigned i = 0; i < count * registers; ++i)
            {
                vertices[i] = UINT64_C(0x1234567800000000) + i;
            }

            for (unsigned state = 0; state < 16; ++state)
            {
                uint64_t prim = (registers == 4 ? 9 : 6) | (state << 5);

                for (unsigned i = 0; i < 1160; ++i)
                {
                    packet[i] = UINT64_MAX;
                }

                scene_batch_encode(packet, prim, count, registers, vertices);
                TEST_ASSERT_TRUE_MESSAGE((packet[0] & 0x7fff) == 1, "(packet[0] & 0x7fff) == 1");
                TEST_ASSERT_TRUE_MESSAGE(((packet[0] >> 58) & 3) == 0, "((packet[0] >> 58) & 3) == 0");
                TEST_ASSERT_TRUE_MESSAGE(packet[0] >> 60 == 1, "packet[0] >> 60 == 1");
                TEST_ASSERT_TRUE_MESSAGE(packet[1] == 14 && packet[2] == prim && packet[3] == 0, "packet[1] == 14 && packet[2] == prim && packet[3] == 0");
                TEST_ASSERT_TRUE_MESSAGE((packet[4] & 0x7fff) == count, "(packet[4] & 0x7fff) == count");
                TEST_ASSERT_TRUE_MESSAGE(((packet[4] >> 15) & 1) == 1, "((packet[4] >> 15) & 1) == 1");
                TEST_ASSERT_TRUE_MESSAGE(((packet[4] >> 58) & 3) == 1, "((packet[4] >> 58) & 3) == 1");
                TEST_ASSERT_TRUE_MESSAGE(packet[4] >> 60 == registers, "packet[4] >> 60 == registers");

                unsigned cursor = 6;

                for (unsigned n = 0; n < count; ++n)
                {
                    for (unsigned r = 0; r < registers; ++r)
                    {
                        unsigned expected = registers == 9   ? (r % 3 == 0 ? 1 : r % 3 == 1 ? 3
                                                                                            : 5)
                                            : registers == 7 ? (r == 0 ? 1 : r & 1 ? 3
                                                                                   : 5)
                                                             : (r == 0 || (registers == 4 && r == 2) ? 1 : 5);

                        TEST_ASSERT_TRUE_MESSAGE(((packet[5] >> (r * 4)) & 15) == expected, "((packet[5] >> (r * 4)) & 15) == expected");
                        TEST_ASSERT_TRUE_MESSAGE(packet[cursor++] == vertices[n * registers + r], "packet[cursor++] == vertices[n * registers + r]");
                    }
                }

                if (cursor & 1)
                {
                    TEST_ASSERT_TRUE_MESSAGE(packet[cursor++] == 0, "packet[cursor++] == 0");
                }

                TEST_ASSERT_TRUE_MESSAGE(cursor == scene_batch_words(count, registers), "cursor == scene_batch_words(count, registers)");
                TEST_ASSERT_TRUE_MESSAGE(packet[cursor] == UINT64_MAX, "packet[cursor] == UINT64_MAX");
            }
        }
    }

    puts("scene batch packets: ordered GS values and padding verified");
}

/**
 * @brief Fixtures are initialized by each scenario.
 */
void setUp(void)
{}

/**
 * @brief This suite owns no external resources.
 */
void tearDown(void)
{}

/**
 * @brief Run the regression scenario.
 * @return Number of failed Unity cases.
 */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(packet_regressions);

    return UNITY_END();
}
