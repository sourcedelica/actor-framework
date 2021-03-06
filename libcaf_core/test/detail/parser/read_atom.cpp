/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE detail.parser.read_atom

#include "caf/detail/parser/read_atom.hpp"

#include "caf/test/unit_test.hpp"

#include <string>

#include "caf/parser_state.hpp"
#include "caf/string_view.hpp"
#include "caf/variant.hpp"

using namespace caf;

namespace {

struct atom_parser_consumer {
  atom_value x;
  void value(atom_value y) {
    x = y;
  }
};

using res_t = variant<pec, atom_value>;

struct atom_parser {
  res_t operator()(string_view str) {
    atom_parser_consumer f;
    string_parser_state res{str.begin(), str.end()};
    detail::parser::read_atom(res, f);
    if (res.code == pec::success)
      return f.x;
    return res.code;
  }
};

struct fixture {
  atom_parser p;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(read_atom_tests, fixture)

CAF_TEST(empty atom) {
  CAF_CHECK_EQUAL(p("''"), atom(""));
  CAF_CHECK_EQUAL(p(" ''"), atom(""));
  CAF_CHECK_EQUAL(p("  ''"), atom(""));
  CAF_CHECK_EQUAL(p("'' "), atom(""));
  CAF_CHECK_EQUAL(p("''  "), atom(""));
  CAF_CHECK_EQUAL(p("  ''  "), atom(""));
  CAF_CHECK_EQUAL(p("\t '' \t\t\t "), atom(""));
}

CAF_TEST(valid atom) {
  CAF_CHECK_EQUAL(p("'abc'"), atom("abc"));
  CAF_CHECK_EQUAL(p("'a b c'"), atom("a b c"));
  CAF_CHECK_EQUAL(p("   'abcdef'   "), atom("abcdef"));
}

CAF_TEST(invalid atoms) {
  CAF_CHECK_EQUAL(p("'abc"), pec::unexpected_eof);
  CAF_CHECK_EQUAL(p("'ab\nc'"), pec::unexpected_newline);
  CAF_CHECK_EQUAL(p("abc"), pec::unexpected_character);
  CAF_CHECK_EQUAL(p("'abc' def"), pec::trailing_character);
  CAF_CHECK_EQUAL(p("'12345678901'"), pec::too_many_characters);
}

CAF_TEST_FIXTURE_SCOPE_END()
