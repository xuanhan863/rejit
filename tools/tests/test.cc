// Copyright (C) 2013 Alexandre Rames <alexandre@coreperf.com>
// rejit is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <iostream>

#include "rejit.h"
#include "checks.h"
#include "flags.h"

using namespace std;

namespace rejit {

#define x10(s) s s s s s s s s s s
#define x50(s) x10(s) x10(s) x10(s) x10(s) x10(s)
#define x100(s) x50(s) x50(s)
#define x200(s) x100(s) x100(s)

// Simple test. Only check if the result is what we expect.
static int Test(MatchType match_type, unsigned expected,
                const char* regexp, const string& text,
                int line);
#define TEST(match_type, expected, regexp, text) \
  failure |= Test(match_type, expected, regexp, string(text), __LINE__);


// Also check the start and end of the match.
static int TestFirst(unsigned expected,
                     const char* regexp,
                     const string& text,
                     unsigned expected_start,
                     unsigned expected_end,
                     int line);
#define TEST_First(expected, re, text, start, end)                             \
  failure |= TestFirst(expected, re, string(text), start, end, __LINE__)
// Also test with non-matching prefixes and suffixes.
// Short strings may not exercise all code paths. This helps exercising
// (for example) the fast forward paths.
static int TestFirstUnbound(unsigned expected,
                            const char* regexp,
                            string text,
                            unsigned expected_start,
                            unsigned expected_end,
                            int line);
#define TEST_First_unbound(expected, re, text, start, end)                     \
  failure |= TestFirstUnbound(expected, re, string(text), start, end, __LINE__)


int RunTest() {
  assert(FLAG_benchtest);
  int failure = 0;

  // Simple characters.
  TEST(kMatchFull, 1, "0123456789", "0123456789");
  TEST(kMatchFull, 0, "0123456789", "0123456789abcd");
  TEST_First_unbound(1, "0123456789", "0123456789",     0, 10);
  TEST_First_unbound(1, "0123456789", "ab0123456789cd", 2, 12);

  // More characters than the maximum number of ring times.
  TEST(kMatchFull, 1, x10("0123456789"), x10("0123456789"));
  TEST(kMatchFull, 0, x10("0123456789"), x10("0123456789") "X");
  TEST(kMatchFull, 0, x10("0123456789"), "X" x10("0123456789"));
  TEST(kMatchFull, 1, x100("0123456789"), x100("0123456789"));
  TEST(kMatchFull, 0, x100("0123456789"), x100("0123456789") "X");
  TEST(kMatchFull, 0, x100("0123456789"), "X" x100("0123456789"));

  // Period.
  TEST(kMatchFull, 1, "01234.6789", "0123456789");
  TEST(kMatchFull, 0, "012345678.", "0123456789abcd");
  TEST_First_unbound(1, ".123456789", "0123456789", 0, 10);
  TEST_First_unbound(1, "012345678.", "ab0123456789cd", 2, 12);
  TEST(kMatchFull, 1, "...", "abc");
  TEST(kMatchFull, 0, ".", "\n");
  TEST(kMatchFull, 0, ".", "\r");
  TEST(kMatchFull, 0, "a.b", "a\nb");
  TEST(kMatchFull, 0, "a.b", "a\rb");
  TEST(kMatchFull, 0, "...", "01");
  TEST(kMatchFull, 0, "..", "012");
  TEST_First(0, "...", "01", 0, 0);
  TEST_First(1, "..", "012", 0, 2);
  TEST_First(0, ".", "\n\n\n\r\r\r", 0, 0);
  TEST_First(1, ".", "\n\n\n\r\r\r.", 6, 7);

  // Start and end of line.
  TEST(kMatchFull, 1, "^" , "");
  TEST(kMatchFull, 1, "$" , "");
  TEST(kMatchFull, 1, "^$", "");
  //TEST(kMatchFull, 0, "$^", ""); // TODO(rames): Interesting! Check the spec.
  TEST(kMatchFull, 1, "^$\n^$" , "\n");
  TEST(kMatchFull, 1, "\n^$"   , "\n");
  TEST(kMatchFull, 1, "^$\n"   , "\n");

  TEST(kMatchFull, 0, "^", "x");
  TEST(kMatchFull, 0, "$", "x");
  TEST(kMatchFull, 0, "^$", "x");
  TEST(kMatchFull, 1, "^\n", "\n");
  TEST(kMatchFull, 1, "\n$", "\n");
  TEST(kMatchFull, 1, "^\n$", "\n");

  TEST_First(1, "^" , "", 0, 0);
  TEST_First(1, "$" , "", 0, 0);
  TEST_First(1, "^$", "", 0, 0);
  TEST_First(1, "^", "xxx", 0, 0);
  TEST_First(1, "$", "xxx", 3, 3);
  TEST_First(0, "^$", "x\nx", 0, 0);
  TEST_First(0, "$^", "x\nx", 0, 0);
  TEST_First(1, "$\n^", "x\nx", 1, 2);
  TEST_First(1, "^x", "012\nx___", 4, 5);
  TEST_First(1, "x$", "012x\n___", 3, 4);
  TEST_First(0, "^x", "012\n___", 0, 0);
  TEST_First(0, "x$", "012\n___", 0, 0);
  TEST_First_unbound(1, "^xxx", "\nxxx_____________", 1, 4);

  TEST(kMatchAll, 1, "^", "___");
  TEST(kMatchAll, 2, "^", "\n");
  TEST(kMatchAll, 3, "^", "\n\n");
  TEST(kMatchAll, 4, "^", "\n\n\n");

  // Alternation.
  TEST(kMatchFull, 1, "0123|abcd|efgh", "abcd");
  TEST(kMatchFull, 1, "0123|abcd|efgh", "efgh");
  TEST(kMatchFull, 0, "0123|abcd|efgh", "_efgh___");
  TEST_First_unbound(1, "0123|abcd|efgh", "_abcd___", 1, 5);
  TEST_First_unbound(0, "0123|abcd|efgh", "_efgX___", 0, 0);
  TEST_First_unbound(1, "(0123|abcd)|efgh", "abcd", 0, 4);
  TEST_First_unbound(1, "0000|1111|2222|3333|4444|5555|6666|7777|8888|9999", "_8888_", 1, 5);
  TEST_First_unbound(0, "0000|1111|2222|3333|4444|5555|6666|7777|8888|9999", "_8__8_", 0, 0);

  // Alternations and ERE.
  TEST(kMatchFull, 1, ")", ")");
  TEST_First_unbound(1, ")", "012)___", 3, 4);

  // Repetition.
  TEST(kMatchFull, 0, "x{3,5}", "x");
  TEST(kMatchFull, 0, "x{3,5}", "xx");
  TEST(kMatchFull, 1, "x{3,5}", "xxx");
  TEST(kMatchFull, 1, "x{3,5}", "xxxx");
  TEST(kMatchFull, 1, "x{3,5}", "xxxxx");
  TEST(kMatchFull, 0, "x{3,5}", "xxxxxx");
  TEST(kMatchFull, 0, "x{3,5}", "xxxxxxxxxxxxx");

  TEST(kMatchFull, 0, "(ab.){3,5}", "ab.");
  TEST(kMatchFull, 0, "(ab.){3,5}", "ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,5}", "ab.ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,5}", "ab.ab.ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,5}", "ab.ab.ab.ab.ab.");
  TEST(kMatchFull, 0, "(ab.){3,5}", "ab.ab.ab.ab.ab.ab.");
  TEST(kMatchFull, 0, "(ab.){3,5}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST(kMatchFull, 1, "(ab.){,5}", "");
  TEST(kMatchFull, 1, "(ab.){,5}", "ab.ab.ab.");
  TEST(kMatchFull, 1, "(ab.){,5}", "ab.ab.ab.ab.ab.");
  TEST(kMatchFull, 0, "(ab.){,5}", "ab.ab.ab.ab.ab.ab.");
  TEST(kMatchFull, 0, "(ab.){,5}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST(kMatchFull, 0, "(ab.){3,}", "");
  TEST(kMatchFull, 0, "(ab.){3,}", "ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,}", "ab.ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,}", "ab.ab.ab.ab.ab.");
  TEST(kMatchFull, 1, "(ab.){3,}", "ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.ab.");

  TEST(kMatchFull, 0, "x{3,5}", "x");
  TEST(kMatchFull, 0, "x{3,5}", "xx");
  TEST(kMatchFull, 1, "x{3,5}", "xxx");
  TEST(kMatchFull, 1, "x{3,5}", "xxxx");
  TEST(kMatchFull, 1, "x{3,5}", "xxxxx");
  TEST(kMatchFull, 0, "x{3,5}", "xxxxxx");

  TEST(kMatchFull, 0, "(a.){2,3}{2,3}", "a.");
  TEST(kMatchFull, 0, "(a.){2,3}{2,3}", "a.a.");
  TEST(kMatchFull, 0, "(a.){2,3}{2,3}", "a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.");
  TEST(kMatchFull, 1, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.a.");
  TEST(kMatchFull, 0, "(a.){2,3}{2,3}", "a.a.a.a.a.a.a.a.a.a.");

  TEST(kMatchFull, 1, ".*", "0123456789");
  TEST(kMatchFull, 1, "0.*9", "0123456789");
  TEST(kMatchFull, 0, "0.*9", "0123456789abcd");
  TEST_First_unbound(1, "0.*9", "0123456789", 0, 10);
  TEST_First_unbound(1, "0.*9", "____0123456789abcd", 4, 14);

  TEST(kMatchFull, 1, "a*b*c*", "aaaabccc");
  TEST(kMatchFull, 1, "a*b*c*", "aaaaccc");
  TEST(kMatchFull, 1, "a*b*c*", "aaaab");
  TEST(kMatchFull, 1, "a*b*c*", "bccc");

  TEST_First_unbound(1, "a+", "012aaa_", 3, 6);
  TEST_First_unbound(1, "(a.)+", "012a.a_a-_", 3, 9);
  TEST_First_unbound(1, "(a.)+", "012a.a_a-_a-", 3, 9);


  // Combinations of alternations and repetitions.
  TEST(kMatchFull, 1, ".**", "0123456789");
  TEST(kMatchFull, 1, "(1|22)*", "111122221221221222222");
  TEST(kMatchFull, 1, "ABCD_(1|22)*_XYZ", "ABCD_111122221221221222222_XYZ");
  TEST(kMatchFull, 0, "ABCD_(1|22)*_XYZ", "111122221221221222222");
  TEST_First_unbound(1, "(1|22)+", "ABCD_111122221221221222222_XYZ", 5, 26);

  TEST(kMatchFull, 1, "(0123|abcd)|(efgh)*", "efghefghefgh");
  TEST(kMatchFull, 1, "(0123|abcd)|(efgh){1,4}", "efghefghefgh");
  TEST(kMatchFull, 1, "(0123|abcd)|(efgh){0,4}", "efghefghefgh");
  TEST(kMatchFull, 0, "(0123|abcd)|(efgh){0,2}", "efghefghefgh");

  // Brackets.
  TEST(kMatchFull, 1, "[0-9]", "0");
  TEST(kMatchFull, 0, "[^0-9]", "0");
  TEST(kMatchFull, 1, "[^0-9]", "a");
  TEST(kMatchFull, 1, "[0-9]abcdefgh", "5abcdefgh");
  TEST(kMatchFull, 0, "[0-9]abcdefgh", "Xabcdefgh");
  TEST(kMatchFull, 1, "a[b-x]g", "afg");
  TEST(kMatchFull, 1, "_[0-9]*_", "__");
  TEST(kMatchFull, 1, "_[0-9]*_", "_1234567890987654321_");
  TEST(kMatchFull, 0, "_[0-9]*_", "_123456789_987654321_");
  TEST_First_unbound(1, "[0-9]", "__________0__________", 10, 11);

  TEST(kMatchFull, 1, "^____$", "____");
  TEST(kMatchFirst, 1, "^____$", "xx\n____");
  TEST(kMatchFirst, 1, "^____$", "____\nxx");
  TEST(kMatchFirst, 1, "^____$", "xx\n____\nxx");

  TEST(kMatchFull, 1, "(abcd|.)*0123", "x0123");
  TEST(kMatchFirst, 1, "[a]{1,}", "________________a___");
  TEST(kMatchFirst, 0, "[a]{1,}", "________________b___");

  TEST(kMatchFull, 0, "(123|(efg)*)456", "123efg456");

  TEST(kMatchFirst, 1, "...123456789", "xxx123456789");
  TEST(kMatchFirst, 0, "...123456789", "xx1234567890");

  TEST(kMatchFirst, 1, "^123456789", "123456789");
  TEST(kMatchFirst, 0, "^123456789", "X1234567890");
  TEST(kMatchFirst, 1, "^(aaa|bbb)", "aaa__");
  TEST(kMatchFirst, 1, "^(aaa|bbb)", "____\naaa__");
  TEST(kMatchFirst, 0, "^(aaa|bbb)", "____aba__");

  TEST(kMatchFirst, 1, "123456789$", "123456789");
  TEST(kMatchFirst, 0, "123456789$", "_123456789_");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa\n");
  TEST(kMatchFirst, 1, "(aaa|bbb)$", "____aaa\n__");
  TEST(kMatchFirst, 0, "(aaa|bbb)$", "____aba__");
  TEST(kMatchFirst, 1, "$(\naaa|\rbbb)", "__\naaa__");

  // Test kMatchAll.
  TEST(kMatchAll, 0, "x", "____________________");
  TEST(kMatchAll, 3, "x", "xxx_________________");
  TEST(kMatchAll, 3, "x", "_________________xxx");
  TEST(kMatchAll, 3, "x", "_x____x____x________");
  TEST(kMatchAll, 4, "x", "_x____xx___x________");
  TEST(kMatchAll, 6, "x", "_x____xx___xxx______");

  TEST(kMatchAll, 0, "ab", "__________________________");
  TEST(kMatchAll, 3, "ab", "ababab____________________");
  TEST(kMatchAll, 3, "ab", "____________________ababab");
  TEST(kMatchAll, 3, "ab", "_ab____ab____ab___________");
  TEST(kMatchAll, 4, "ab", "_ab____abab___ab__________");
  TEST(kMatchAll, 6, "ab", "_ab____abab___ababab______");

  TEST(kMatchAll, 0, "a.", "__________________________");
  TEST(kMatchAll, 3, "a.", "a.a.a._____________________");
  TEST(kMatchAll, 3, "a.", "____________________a.a.a.");
  TEST(kMatchAll, 3, "a.", "_a.____a.____a.___________");
  TEST(kMatchAll, 4, "a.", "_a.____a.a.___a.__________");
  TEST(kMatchAll, 6, "a.", "_a.____a.a.___a.a.a.______");

  TEST(kMatchAll, 4, "x+", "_x__xxx____x____xxxxxx_________");
  TEST(kMatchAll, 4, "(a.)+", "_a.__a.a.a.____a.____a.a.a.a.a.a._________");
  TEST(kMatchAll, 4, "x+", "x__xxx____x____xxxxxx");
  TEST(kMatchAll, 4, "(a.)+", "a.__a.a.a.____a.____a.a.a.a.a.a.");

  // Alternation of fast forward elements.
  TEST_First_unbound(1, "(0|0)", "0", 0, 1);
  TEST_First_unbound(1, "(01|01)", "01", 0, 2);
  TEST_First_unbound(1, "(012|012)", "012", 0, 3);
  TEST_First_unbound(1, "(0123|0123)", "0123", 0, 4);
  TEST_First_unbound(1, "(01234|01234)", "01234", 0, 5);
  TEST_First_unbound(1, "(012345|012345)", "012345", 0, 6);
  TEST_First_unbound(1, "(0123456|0123456)", "0123456", 0, 7);
  TEST_First_unbound(1, "(01234567|01234567)", "01234567", 0, 8);
  TEST_First_unbound(1, "(012345678|012345678)", "012345678", 0, 9);
  TEST_First_unbound(1, "(0123456789|0123456789)", "0123456789", 0, 10);
  TEST_First(1, "(xxx|$)", "___xxx___", 3, 6);
  TEST_First(1, "(xxx|^)", "___xxx___", 0, 0);
  TEST_First_unbound(1, "(xxx|[ab-d])", "___ab___xxx___", 3, 4);
  TEST_First(1, "(xxx|^|$)", "___xxx___", 0, 0);
  TEST(kMatchAll, 3, "(xxx|^|$)", "___xxx___");
  TEST(kMatchAll, 6, "(xxx|^|$)", "___xxx_\n\n__");
  TEST(kMatchAll, 8, "(xxx|^|$|[ab-d])", "___ab___xxx_\n\n__");
  TEST(kMatchAll, 4, "(^|$|[^x])", "_xxx_x_");

  // Special matching patterns.
  TEST(kMatchFull, 1, "\\d", "5");
  TEST(kMatchFull, 0, "\\d", "_");
  TEST(kMatchFull, 0, "\\D", "5");
  TEST(kMatchFull, 1, "\\D", "_");
  TEST(kMatchFull, 1, "\\n", "\n");
  TEST(kMatchFull, 0, "\\n", "\r");
  TEST(kMatchFull, 1, "\\s", " ");
  TEST(kMatchFull, 1, "\\s", "\t");
  TEST(kMatchFull, 0, "\\s", "_");
  TEST(kMatchFull, 0, "\\s", "_");
  TEST(kMatchFull, 0, "\\S", " ");
  TEST(kMatchFull, 0, "\\S", "\t");
  TEST(kMatchFull, 1, "\\S", "_");
  TEST(kMatchFull, 1, "\\S", "_");
  TEST(kMatchFull, 1, "\\t", "\t");
  TEST(kMatchFull, 0, "\\t", "\n");
  TEST(kMatchFull, 1, "\\x30", "0");
  TEST(kMatchFull, 0, "\\x30", "_");

  return failure;
}


static int Test(MatchType match_type, unsigned expected,
                const char* regexp, const string& text,
                int line) {
  bool exception = false;
  unsigned res = 0;

  if (match_type == kMatchFirst) {
    int rc;
    // If there is a first match, there is a match somewhere.
    if ((rc = Test(kMatchAnywhere, expected, regexp, text.c_str(), line))) {
      return rc;
    }
  }

  try {
    Regej re(regexp);
    vector<Match> matches;
    Match         match;
    switch (match_type) {
      case kMatchFull:
        res = re.MatchFull(text);
        break;
      case kMatchAnywhere:
        res = re.MatchAnywhere(text);
        break;
      case kMatchFirst:
        res = re.MatchFirst(text, &match);
        break;
      case kMatchAll:
        re.MatchAll(text, &matches);
        res = matches.size();
        break;

      default:
        UNREACHABLE();
    }
  } catch (int e) {
    exception = true;
  }

  if (exception || res != expected) {
    cout << "--- FAILED rejit test line " << line
         << " ------------------------------------------------------" << endl;
    cout << "regexp:\n" << regexp << endl;
    cout << "text:\n" << text << endl;
    cout << "expected: " << expected << "  found: " << res << endl;
    SET_FLAG(trace_repetitions, true);
    SET_FLAG(print_re_tree, true);
    SET_FLAG(print_re_list, true);
    SET_FLAG(print_ff_elements, true);
    SET_FLAG(print_state_ring_info, true);
    try {
      Regej re(regexp);
      vector<Match> matches;
      Match         match;
      switch (match_type) {
        case kMatchFull:
          res = re.MatchFull(text);
          break;
        case kMatchAnywhere:
          res = re.MatchAnywhere(text);
          break;
        case kMatchFirst:
          res = re.MatchFirst(text, &match);
          break;
        case kMatchAll:
          re.MatchAll(text, &matches);
          res = matches.size();
          break;

        default:
          UNREACHABLE();
      }
    } catch (int e) {}
    SET_FLAG(trace_repetitions, false);
    SET_FLAG(print_re_tree, false);
    SET_FLAG(print_re_list, false);
    SET_FLAG(print_ff_elements, false);
    SET_FLAG(print_state_ring_info, false);
    cout << "------------------------------------------------------------------------------------\n\n" << endl;
  }
  return exception || res != expected;
}

static int TestFirst(unsigned expected,
                     const char* regexp,
                     const string& text,
                     unsigned expected_start,
                     unsigned expected_end,
                     int line) {
  bool exception = false;
  unsigned res, found_start, found_end;

  int rc;
  if ((rc = Test(kMatchAnywhere, expected, regexp, text, line))) {
    return rc;
  }

  try {
    Regej re(regexp);
    Match match;
    res = re.MatchFirst(text, &match);
    found_start = match.begin - text.c_str();
    found_end = match.end - text.c_str();
    exception = (expected == 1) &&
      (found_end != expected_end || found_start != expected_start);

  } catch (int e) {
    exception = true;
  }

  if (exception || (res != expected)) {
    cout << "--- FAILED rejit test line " << line
         << " ------------------------------------------------------" << endl;
    cout << "regexp:\n" << regexp << endl;
    cout << "text:\n" << text << endl;
    cout << "expected: " << expected << "  found: " << res << endl;
    cout << "found    start:end: " << found_start << ":" << found_end << endl;
    cout << "expected start:end: " << expected_start << ":" << expected_end << endl;
    SET_FLAG(trace_repetitions, true);
    SET_FLAG(print_re_tree, true);
    SET_FLAG(print_re_list, true);
    SET_FLAG(print_ff_elements, true);
    SET_FLAG(print_state_ring_info, true);

    try {
      Regej re(regexp);
      Match match;
      res = re.MatchFirst(text, &match);
    } catch (int e) {}

    SET_FLAG(trace_repetitions, false);
    SET_FLAG(print_re_tree, false);
    SET_FLAG(print_re_list, false);
    SET_FLAG(print_ff_elements, false);
    SET_FLAG(print_state_ring_info, false);
    cout << "------------------------------------------------------------------------------------\n\n" << endl;
  }
  return exception || res != expected;
}


static int TestFirstUnbound(unsigned expected,
                            const char* regexp,
                            string text,
                            unsigned expected_start,
                            unsigned expected_end,
                            int line) {
  int rc = 0;
  if ((rc = TestFirst(expected, regexp, text,
                      expected_start, expected_end, line))) {
    return rc;
  }
  text.append(x200(" "));
  if ((rc = TestFirst(expected, regexp, text,
                      expected_start, expected_end, line))) {
    return rc;
  }
  text.insert(0, x200(" "));
  if ((rc = TestFirst(expected, regexp, text,
                      200 + expected_start, 200 + expected_end, line))) {
    return rc;
  }
  for (unsigned i = 1; i <= 64; i++) {
    text.insert(0, " ");
    if ((rc = TestFirst(expected, regexp, text,
                        200 + i + expected_start, 200 + i + expected_end, line))) {
      return rc;
    }
  }
  return rc;
}

#undef x10
#undef x100
#undef TEST
#undef TEST_First

}  // namespace rejit


int main() {
  int rc = rejit::RunTest();
  if (rc) {
    printf("FAILED\n");
  } else {
    printf("success\n");
  }
  return rc;
}

#undef TEST
