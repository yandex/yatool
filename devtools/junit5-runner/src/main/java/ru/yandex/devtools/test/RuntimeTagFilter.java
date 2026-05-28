package ru.yandex.devtools.test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

import org.junit.platform.engine.FilterResult;
import org.junit.platform.engine.TestDescriptor;
import org.junit.platform.engine.TestTag;
import org.junit.platform.launcher.PostDiscoveryFilter;

public class RuntimeTagFilter implements PostDiscoveryFilter {

    private final YaTestNameBase testName;
    private final List<JunitTagsClause> clauses;

    public RuntimeTagFilter(YaTestNameBase testName, List<String> rawExpressions) {
        this.testName = testName;
        this.clauses = JunitTagsClause.parseAll(rawExpressions);
    }

    static String describeExpression(String expr) {
        return JunitTagsClause.parseOne(expr).toString();
    }

    @Override
    public FilterResult apply(TestDescriptor test) {
        if (clauses.isEmpty()) {
            return FilterResult.included("");
        }
        if (!testName.isTest(test)) {
            return FilterResult.excluded("No suitable tags found");
        }

        Set<String> tagSet = collectInheritedTags(test);
        for (JunitTagsClause clause : clauses) {
            if (clause.matches(tagSet)) {
                return FilterResult.included("Matched junit-tags clause: " + clause);
            }
        }
        return FilterResult.excluded("No suitable tags found");
    }

    private static Set<String> collectInheritedTags(TestDescriptor test) {
        Set<String> result = new HashSet<>();
        TestDescriptor current = test;
        while (current != null) {
            for (TestTag testTag : current.getTags()) {
                result.add(testTag.getName());
            }
            current = current.getParent().orElse(null);
        }
        return result;
    }

    private static final class JunitTagsClause {
        private final TagSegment positiveSegment;
        private final List<TagSegment> forbiddenSegments;

        private JunitTagsClause(TagSegment positiveSegment, List<TagSegment> forbiddenSegments) {
            this.positiveSegment = positiveSegment;
            this.forbiddenSegments = forbiddenSegments;
        }

        static List<JunitTagsClause> parseAll(List<String> rawExpressions) {
            List<JunitTagsClause> out = new ArrayList<>();
            for (String raw : rawExpressions) {
                if (raw == null) {
                    continue;
                }
                String trimmed = raw.trim();
                if (trimmed.isEmpty()) {
                    continue;
                }
                JunitTagsClause clause = parseOne(trimmed);
                if (clause != null) {
                    out.add(clause);
                }
            }
            return out;
        }

        static JunitTagsClause parseOne(String expr) {
            List<TagSegment> segments = parseSegments(expr);
            TagSegment positive = segments.get(0);
            List<TagSegment> forbidden = new ArrayList<>(segments.subList(1, segments.size()));
            return new JunitTagsClause(positive, forbidden);
        }

        private static List<TagSegment> parseSegments(String expr) {
            String normalized = expr.trim().replaceAll("\\s*\\+\\s*", "+");
            if (normalized.isEmpty()) {
                throw invalidExpression(expr, "required part is empty");
            }

            List<TagSegment> segments = new ArrayList<>();
            List<String> currentTerms = new ArrayList<>();
            boolean parsingRequiredPart = true;
            for (String token : normalized.split("\\s+")) {
                if ("-".equals(token)) {
                    segments.add(TagSegment.parse(currentTerms, expr, parsingRequiredPart ? "required part" : "exclusion part"));
                    currentTerms = new ArrayList<>();
                    parsingRequiredPart = false;
                    continue;
                }
                currentTerms.add(token);
            }
            segments.add(TagSegment.parse(currentTerms, expr, parsingRequiredPart ? "required part" : "exclusion part"));
            return segments;
        }

        boolean matches(Set<String> tagsOnTest) {
            if (!positiveSegment.matches(tagsOnTest)) {
                return false;
            }
            for (TagSegment forbiddenSegment : forbiddenSegments) {
                if (forbiddenSegment.matches(tagsOnTest)) {
                    return false;
                }
            }
            return true;
        }

        @Override
        public String toString() {
            StringBuilder builder = new StringBuilder(positiveSegment.toString());
            for (TagSegment forbiddenSegment : forbiddenSegments) {
                builder.append(" - ").append(forbiddenSegment);
            }
            return builder.toString();
        }
    }

    private static final class TagSegment {
        private final List<TagTerm> terms;

        private TagSegment(List<TagTerm> terms) {
            this.terms = terms;
        }

        static TagSegment parse(List<String> rawTerms, String expression, String partName) {
            if (rawTerms.isEmpty()) {
                throw invalidExpression(expression, partName + " is empty");
            }

            List<TagTerm> terms = new ArrayList<>();
            for (String rawTerm : rawTerms) {
                terms.add(TagTerm.parse(rawTerm, expression));
            }
            return new TagSegment(terms);
        }

        boolean matches(Set<String> tagsOnTest) {
            for (TagTerm term : terms) {
                if (!term.matches(tagsOnTest)) {
                    return false;
                }
            }
            return true;
        }

        @Override
        public String toString() {
            return terms.stream()
                    .map(TagTerm::toString)
                    .collect(Collectors.joining(" "));
        }
    }

    private static final class TagTerm {
        private final List<String> alternativeTags;

        private TagTerm(List<String> alternativeTags) {
            this.alternativeTags = alternativeTags;
        }

        static TagTerm parse(String rawTerm, String expression) {
            List<String> alternativeTags = Arrays.stream(rawTerm.split("\\+"))
                    .map(String::trim)
                    .filter(s -> !s.isEmpty())
                    .collect(Collectors.toList());
            if (alternativeTags.isEmpty()) {
                throw invalidExpression(expression, "term is empty");
            }
            return new TagTerm(alternativeTags);
        }

        boolean matches(Set<String> tagsOnTest) {
            for (String tag : alternativeTags) {
                if (tagsOnTest.contains(tag)) {
                    return true;
                }
            }
            return false;
        }

        @Override
        public String toString() {
            return String.join("+", alternativeTags);
        }
    }

    private static IllegalArgumentException invalidExpression(String expression, String reason) {
        return new IllegalArgumentException("Invalid --junit-tags expression '" + expression + "': " + reason);
    }
}
