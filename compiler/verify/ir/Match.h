#pragma once

#include <verify/ir/Function.h>

#include <string>
#include <string_view>

namespace verify::ir {

//! A placeholder of a pattern, standing for an arbitrary expression
struct Placeholder : SmallHandle {
    using SmallHandle::SmallHandle;
};

//! A placeholder of a pattern, standing for an arbitrary code position
struct LabelPlaceholder : SmallHandle {
    using SmallHandle::SmallHandle;
};

//! A shape that expressions can be matched against
/*!
A pattern is written in the text form of the IR as a function stating a single proposition. Its
parameters are the placeholders for arbitrary expressions, its labels the placeholders for
arbitrary code positions and Sort::UninterpretedConstant is a placeholder for an arbitrary sort.
*/
struct Pattern {
    explicit Pattern(const char* source);

    //! The placeholder written as the parameter '$name'
    Placeholder placeholder(std::string_view name) const;
    //! The placeholder written as the label '@name'
    LabelPlaceholder label(std::string_view name) const;

    //! The proposition the pattern states
    Bool prop() const { return m_prop; }
    const Function& function() const { return m_function; }

    //! Where a code position of the pattern sits relative to the positions it is tied to
    /*!
    Two positions belong to the same run when no 'nop' separates them. Matching one position of
    a run fixes every other position of it.
    */
    struct PositionInfo {
        uint32_t run;
        uint32_t offset;
    };
    const PositionInfo& positionInfo(CodePos pos) const {
        VERIFY(pos.id() < m_positions.size());
        return m_positions[pos.id()];
    }
    int_t runCount() const { return m_runCount; }

private:
    Function m_function;
    Bool m_prop { false };
    std::vector<std::string> m_parameterNames;
    std::vector<std::pair<std::string, CodePos>> m_labels;
    std::vector<PositionInfo> m_positions;
    int_t m_runCount = 0;
};

//! The values the placeholders of a pattern were matched to
/*!
This will keep a reference to the pattern it matches, so the pattern must outlive its matches.
*/
struct Match {
    explicit Match(const Pattern& pattern);

    Expr operator[](Placeholder placeholder) const {
        VERIFY(placeholder.id() < m_placeholders.size());
        VERIFY(m_placeholders[placeholder.id()].has_value());
        return *m_placeholders[placeholder.id()];
    }
    CodePos operator[](LabelPlaceholder label) const {
        std::optional<CodePos> pos = position(CodePos(label.id()));
        VERIFY(pos.has_value());
        return *pos;
    }
    //! The sort that a sort named by the pattern was matched to
    Sort uninterpretedSort() const {
        VERIFY(m_uninterpretedSort.has_value());
        return *m_uninterpretedSort;
    }

    //! The position a code position of the pattern was matched to, if its run was matched at all
    std::optional<CodePos> position(CodePos patternPos) const;

private:
    friend struct Matcher;

    const Pattern& m_pattern;
    std::vector<std::optional<Expr>> m_placeholders;
    //! The position the first position of every run was matched to
    std::vector<std::optional<CodePos>> m_runBase;
    std::optional<Sort> m_uninterpretedSort;
    //! The edge every edge of the pattern was matched to
    std::vector<std::optional<ControlFlowEdge>> m_edges;
};

//! Matches the proposition 'prop' of 'function' against the shape of 'pattern'
std::optional<Match> matchClause(const Pattern& pattern, const Function& function, Bool prop);

}
