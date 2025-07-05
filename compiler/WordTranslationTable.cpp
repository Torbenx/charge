
#include <WordTranslationTable.h>

#include <gtest/gtest.h>

namespace {
    Word withId(Word w, uint8_t id) {
        w.setId(id);
        return w;
    }
}

TEST(WordTable, Translation) {
    Word foo { 0, Word::hash("foo") };
    Word bar { 0, Word::hash("bar") };
    Word baz { 0, Word::hash("baz") };

    {
        WordTranslationTable table;
        for (int_t i = 0; i <= Word::MAX_ID; i++) {
            table.insert(withId(foo, i), withId(foo, Word::MAX_ID - i));
            if (i % 2 == 0)
                table.insert(withId(bar, i), withId(bar, Word::MAX_ID - i));
        }

        for (int_t i = 0; i <= Word::MAX_ID; i++) {
            EXPECT_EQ(table.get(withId(foo, i)), withId(foo, Word::MAX_ID - i));
            EXPECT_EQ(table.get(withId(bar, i)).id(), withId(bar, i % 2 == 0 ? Word::MAX_ID - i : i).id());
        }
        EXPECT_EQ(table.get(baz), baz);
    }
}