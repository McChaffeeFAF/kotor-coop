/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// The test-only NWScript compiler (test/support/nsscomp.h): programs it compiles
// run on the script VM with the results the language promises, and survive a trip
// through the NCS file format.

#include <functional>

#include <gtest/gtest.h>

#include "../support/nsscomp.h"

#include "reone/script/executioncontext.h"
#include "reone/script/executionstate.h"
#include "reone/script/routine.h"
#include "reone/script/routines.h"
#include "reone/script/variable.h"
#include "reone/script/virtualmachine.h"

using namespace reone;
using namespace reone::script;
using namespace reone::testsupport;

namespace {

// A routine table of a few recording routines, in the shape of the engine's.
class FakeRoutines : public IRoutines {
public:
    std::vector<std::string> out;
    std::vector<std::shared_ptr<ExecutionContext>> delayed;

    FakeRoutines() {
        add("Out", VariableType::Void, {VariableType::Int}, [this](const std::vector<Variable> &args) {
            out.push_back(std::to_string(args[0].intValue));
            return Variable::ofNull();
        });
        add("OutF", VariableType::Void, {VariableType::Float}, [this](const std::vector<Variable> &args) {
            out.push_back(std::to_string(args[0].floatValue));
            return Variable::ofNull();
        });
        add("OutS", VariableType::Void, {VariableType::String}, [this](const std::vector<Variable> &args) {
            out.push_back(args[0].strValue);
            return Variable::ofNull();
        });
        add("OutO", VariableType::Void, {VariableType::Object}, [this](const std::vector<Variable> &args) {
            out.push_back("o" + std::to_string(args[0].objectId));
            return Variable::ofNull();
        });
        add("OutV", VariableType::Void, {VariableType::Vector}, [this](const std::vector<Variable> &args) {
            out.push_back(std::to_string(args[0].vecValue.x) + "," + std::to_string(args[0].vecValue.y) + "," + std::to_string(args[0].vecValue.z));
            return Variable::ofNull();
        });
        // Argument order is observable: the result lists the arguments in order.
        add("Cat3", VariableType::String, {VariableType::String, VariableType::String, VariableType::String}, [](const std::vector<Variable> &args) {
            return Variable::ofString(args[0].strValue + args[1].strValue + args[2].strValue);
        });
        add("Sub", VariableType::Int, {VariableType::Int, VariableType::Int}, [](const std::vector<Variable> &args) {
            return Variable::ofInt(args[0].intValue - args[1].intValue);
        });
        add("Double", VariableType::Float, {VariableType::Float}, [](const std::vector<Variable> &args) {
            return Variable::ofFloat(args[0].floatValue * 2.0f);
        });
        add("MakeVec", VariableType::Vector, {VariableType::Float, VariableType::Float, VariableType::Float}, [](const std::vector<Variable> &args) {
            return Variable::ofVector(glm::vec3(args[0].floatValue, args[1].floatValue, args[2].floatValue));
        });
        add("VecLen2", VariableType::Float, {VariableType::Vector}, [](const std::vector<Variable> &args) {
            return Variable::ofFloat(glm::dot(args[0].vecValue, args[0].vecValue));
        });
        add("Obj", VariableType::Object, {VariableType::Int}, [](const std::vector<Variable> &args) {
            return Variable::ofObject(static_cast<uint32_t>(args[0].intValue));
        });
        add("TwoArgs", VariableType::Int, {VariableType::Int, VariableType::Int, VariableType::Int}, [](const std::vector<Variable> &args) {
            // Optional arguments arrive only when the script gave them.
            return Variable::ofInt(static_cast<int>(args.size()) * 100 + args[0].intValue * 10 + (args.size() > 1 ? args[1].intValue : 0));
        });
        // Runs the action at once, as the object (the engine runs it as that object).
        add("AssignCommand", VariableType::Void, {VariableType::Object, VariableType::Action}, [this](const std::vector<Variable> &args) {
            run(*args[1].context);
            return Variable::ofNull();
        });
        // Keeps the action for the test to run later.
        add("DelayCommand", VariableType::Void, {VariableType::Float, VariableType::Action}, [this](const std::vector<Variable> &args) {
            delayed.push_back(std::make_shared<ExecutionContext>(*args[1].context));
            return Variable::ofNull();
        });
    }

    Routine &get(int index) override { return _routines[index]; }
    int getNumRoutines() const override { return static_cast<int>(_routines.size()); }
    int getIndexByName(const std::string &name) const override {
        for (size_t i = 0; i < _routines.size(); ++i) {
            if (_routines[i].name() == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void run(const ExecutionContext &closure) {
        auto ctx = std::make_unique<ExecutionContext>(closure);
        ctx->routines = this;
        auto program = ctx->savedState->program;
        VirtualMachine(program, std::move(ctx)).run();
    }

private:
    std::vector<Routine> _routines;

    void add(const std::string &name, VariableType ret, std::vector<VariableType> args,
             std::function<Variable(const std::vector<Variable> &)> fn) {
        _routines.emplace_back(name, ret, Variable::ofNull(), std::move(args),
                               [fn](const std::vector<Variable> &a, ExecutionContext &) { return fn(a); });
    }
};

struct Result {
    int value {0};
    std::vector<std::string> out;
};

Result run(const std::string &source, bool viaNcs = false, const NssOptions &options = {}) {
    FakeRoutines routines;
    auto program = viaNcs ? compileNssRoundTrip("test", source, routines, options) : compileNss("test", source, routines, options);
    auto ctx = std::make_unique<ExecutionContext>();
    ctx->routines = &routines;
    Result result;
    result.value = VirtualMachine(program, std::move(ctx)).run();
    result.out = routines.out;
    return result;
}

using Out = std::vector<std::string>;

} // namespace

// Expressions ---------------------------------------------------------------------

TEST(NssCompiler, evaluates_integer_arithmetic_with_precedence_and_bit_operators) {
    auto r = run("void main() { Out(1 + 2 * 3); Out((1 + 2) * 3); Out(17 % 5); Out(7 / 2); Out(1 << 4); Out(256 >> 2); Out(6 & 3); Out(6 | 3); Out(6 ^ 3); Out(~0); Out(-5 + 2); Out(10 - 3 - 2); }");
    EXPECT_EQ((Out {"7", "9", "2", "3", "16", "64", "2", "7", "5", "-1", "-3", "5"}), r.out);
}

TEST(NssCompiler, evaluates_comparisons_and_logic_to_zero_or_one) {
    auto r = run("void main() { Out(1 < 2); Out(2 < 1); Out(2 <= 2); Out(3 > 4); Out(4 >= 4); Out(1 == 1); Out(1 != 1); Out(1 && 0); Out(1 || 0); Out(!0); Out(!5); Out(TRUE); Out(FALSE); }");
    EXPECT_EQ((Out {"1", "0", "1", "0", "1", "1", "0", "0", "1", "1", "0", "1", "0"}), r.out);
}

TEST(NssCompiler, widens_ints_to_floats_where_the_language_does) {
    auto r = run("void main() { float f = 3; OutF(f); OutF(1 + 0.5); OutF(0.5 + 1); OutF(7 / 2.0); Out(1.5 < 2); Out(2 == 2.0); float g = 2; g += 1; OutF(g); OutF(Double(4)); }");
    ASSERT_EQ(8u, r.out.size());
    EXPECT_EQ("3.000000", r.out[0]);
    EXPECT_EQ("1.500000", r.out[1]);
    EXPECT_EQ("1.500000", r.out[2]);
    EXPECT_EQ("3.500000", r.out[3]);
    EXPECT_EQ("1", r.out[4]);
    EXPECT_EQ("1", r.out[5]);
    EXPECT_EQ("3.000000", r.out[6]);
    EXPECT_EQ("8.000000", r.out[7]);
}

TEST(NssCompiler, concatenates_and_compares_strings) {
    auto r = run("void main() { string a = \"foo\"; string b = a + \"bar\" + \"\\n\"; OutS(b); Out(a == \"foo\"); Out(a != \"foo\"); OutS(\"quote \\\" slash \\\\\"); }");
    EXPECT_EQ((Out {"foobar\n", "1", "0", "quote \" slash \\"}), r.out);
}

TEST(NssCompiler, handles_hex_literals_comments_and_ternaries) {
    auto r = run("// line\nvoid main() { /* block */ Out(0x1F); Out(1 ? 10 : 20); Out(0 ? 10 : 20); OutF(1 ? 1 : 2.5); }");
    ASSERT_EQ(4u, r.out.size());
    EXPECT_EQ("31", r.out[0]);
    EXPECT_EQ("10", r.out[1]);
    EXPECT_EQ("20", r.out[2]);
    EXPECT_EQ("1.000000", r.out[3]);
}

TEST(NssCompiler, objects_are_compared_and_the_constants_are_defined) {
    auto r = run("void main() { object a = Obj(5); object b = Obj(5); Out(a == b); Out(a != OBJECT_INVALID); OutO(OBJECT_SELF); OutO(OBJECT_INVALID); OutO(a); }");
    EXPECT_EQ((Out {"1", "1", "o1", "o1", "o5"}), r.out) << "without a caller OBJECT_SELF reads as invalid";
}

TEST(NssCompiler, extra_constants_come_from_the_options) {
    NssOptions options;
    options.intConstants["OBJECT_TYPE_CREATURE"] = 1;
    options.floatConstants["PI"] = 3.5f;
    options.stringConstants["GREETING"] = "hi";
    auto r = run("void main() { Out(OBJECT_TYPE_CREATURE + 1); OutF(PI); OutS(GREETING); }", false, options);
    EXPECT_EQ((Out {"2", "3.500000", "hi"}), r.out);
}

// Vectors --------------------------------------------------------------------------

TEST(NssCompiler, vectors_have_components_and_arithmetic) {
    auto r = run("void main() { vector v = [1.0, 2.0, 3.0]; OutF(v.x); OutF(v.y); OutF(v.z); OutV(v); OutV(v + v); OutV(v * 2.0); OutV(2.0 * v); OutV(v / 2.0); OutV(v - [1.0, 1.0, 1.0]); v.y = 9.0; OutV(v); Out(v == [1.0, 9.0, 3.0]); Out(v != [1.0, 9.0, 3.0]); }");
    ASSERT_EQ(12u, r.out.size());
    EXPECT_EQ("1.000000", r.out[0]);
    EXPECT_EQ("2.000000", r.out[1]);
    EXPECT_EQ("3.000000", r.out[2]);
    EXPECT_EQ("1.000000,2.000000,3.000000", r.out[3]);
    EXPECT_EQ("2.000000,4.000000,6.000000", r.out[4]);
    EXPECT_EQ("2.000000,4.000000,6.000000", r.out[5]);
    EXPECT_EQ("2.000000,4.000000,6.000000", r.out[6]);
    EXPECT_EQ("0.500000,1.000000,1.500000", r.out[7]);
    EXPECT_EQ("0.000000,1.000000,2.000000", r.out[8]);
    EXPECT_EQ("1.000000,9.000000,3.000000", r.out[9]);
    EXPECT_EQ("1", r.out[10]);
    EXPECT_EQ("0", r.out[11]);
}

TEST(NssCompiler, vectors_cross_the_routine_boundary_in_both_directions) {
    auto r = run("void main() { vector v = MakeVec(1.0, 2.0, 3.0); OutF(v.x); OutF(v.z); OutF(MakeVec(4.0, 5.0, 6.0).y); OutF(VecLen2([1.0, 2.0, 2.0])); OutF(VecLen2(v)); }");
    ASSERT_EQ(5u, r.out.size());
    EXPECT_EQ("1.000000", r.out[0]) << "x of a returned vector";
    EXPECT_EQ("3.000000", r.out[1]) << "z of a returned vector";
    EXPECT_EQ("5.000000", r.out[2]);
    EXPECT_EQ("9.000000", r.out[3]);
    EXPECT_EQ("14.000000", r.out[4]);
}

// Variables ----------------------------------------------------------------------------

TEST(NssCompiler, locals_assign_compound_assign_and_step) {
    auto r = run("void main() { int a = 5; int b; b = a; a += 3; a -= 1; a *= 2; a /= 7; Out(a); Out(b); int i = 0; Out(i++); Out(i); Out(++i); Out(i--); Out(--i); a = b = 4; Out(a + b); int c = 1, d = 2; Out(c + d); }");
    EXPECT_EQ((Out {"2", "5", "0", "1", "2", "2", "0", "8", "3"}), r.out);
}

TEST(NssCompiler, globals_are_initialised_in_order_and_shared_by_functions) {
    auto r = run("int g_a = 3;\nint g_b = g_a * 2;\nstring g_s = \"x\";\nvoid bump() { g_a += 10; g_b++; }\nvoid main() { Out(g_a); Out(g_b); bump(); Out(g_a); Out(g_b); OutS(g_s); g_s = g_s + \"y\"; OutS(g_s); }");
    EXPECT_EQ((Out {"3", "6", "13", "7", "x", "xy"}), r.out);
}

TEST(NssCompiler, inner_scopes_shadow_and_release_their_variables) {
    auto r = run("int x = 1;\nvoid main() { Out(x); int x = 2; Out(x); { int x = 3; Out(x); } Out(x); for (int x = 0; x < 2; x++) { Out(x + 100); } Out(x); }");
    EXPECT_EQ((Out {"1", "2", "3", "2", "100", "101", "2"}), r.out);
}

// Control flow ----------------------------------------------------------------------------

TEST(NssCompiler, if_else_chains_pick_one_branch) {
    auto r = run("void check(int n) { if (n < 0) Out(-1); else if (n == 0) Out(0); else Out(1); }\nvoid main() { check(-5); check(0); check(7); if (0) Out(99); }");
    EXPECT_EQ((Out {"-1", "0", "1"}), r.out);
}

TEST(NssCompiler, loops_run_with_break_and_continue) {
    auto r = run("void main() { int i = 0; while (i < 3) { Out(i); i++; } "
                 "for (int j = 0; j < 6; j++) { if (j == 1) continue; if (j == 4) break; Out(10 + j); } "
                 "int k = 0; do { Out(20 + k); k++; } while (k < 2); "
                 "for (;;) { int local = 5; if (local == 5) break; } Out(30); "
                 "i = 0; while (1) { int a = 1; int b = 2; i++; if (i == 2) continue; if (i > 3) break; Out(40 + i + a + b - 3); } }");
    EXPECT_EQ((Out {"0", "1", "2", "10", "12", "13", "20", "21", "30", "41", "43"}), r.out);
}

// Functions --------------------------------------------------------------------------------

TEST(NssCompiler, user_functions_return_values_and_recurse) {
    auto r = run("int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }\n"
                 "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n"
                 "float half(float f) { return f / 2.0; }\n"
                 "string twice(string s) { return s + s; }\n"
                 "void main() { Out(fact(5)); Out(fib(10)); OutF(half(5.0)); OutS(twice(\"ab\")); OutF(half(3)); }");
    ASSERT_EQ(5u, r.out.size());
    EXPECT_EQ("120", r.out[0]);
    EXPECT_EQ("55", r.out[1]);
    EXPECT_EQ("2.500000", r.out[2]);
    EXPECT_EQ("abab", r.out[3]);
    EXPECT_EQ("1.500000", r.out[4]) << "an int argument widens to a float parameter";
}

TEST(NssCompiler, arguments_arrive_in_order_for_user_functions_and_routines) {
    auto r = run("int diff(int a, int b, int c) { return a * 100 + b * 10 + c; }\n"
                 "string pick(string a, int n, string b) { if (n) return a; return b; }\n"
                 "void main() { Out(diff(1, 2, 3)); Out(Sub(10, 3)); OutS(Cat3(\"a\", \"b\", \"c\")); OutS(pick(\"left\", 1, \"right\")); OutS(pick(\"left\", 0, \"right\")); }");
    EXPECT_EQ((Out {"123", "7", "abc", "left", "right"}), r.out);
}

TEST(NssCompiler, optional_routine_arguments_may_be_left_out) {
    auto r = run("void main() { Out(TwoArgs(1)); Out(TwoArgs(1, 2)); Out(TwoArgs(1, 2, 3)); }");
    EXPECT_EQ((Out {"110", "212", "312"}), r.out);
}

TEST(NssCompiler, functions_may_be_declared_first_and_defined_later) {
    auto r = run("int odd(int n);\nint even(int n) { if (n == 0) return 1; return odd(n - 1); }\nint odd(int n) { if (n == 0) return 0; return even(n - 1); }\nvoid main() { Out(even(10)); Out(odd(10)); Out(odd(7)); }");
    EXPECT_EQ((Out {"1", "0", "1"}), r.out);
}

TEST(NssCompiler, functions_return_vectors_and_take_them) {
    auto r = run("vector scale(vector v, float k) { return v * k; }\nfloat sum(vector v) { return v.x + v.y + v.z; }\nvoid main() { vector s = scale([1.0, 2.0, 3.0], 2.0); OutV(s); OutF(sum(s)); OutF(sum(scale(s, 0.5))); }");
    ASSERT_EQ(3u, r.out.size());
    EXPECT_EQ("2.000000,4.000000,6.000000", r.out[0]);
    EXPECT_EQ("12.000000", r.out[1]);
    EXPECT_EQ("6.000000", r.out[2]);
}

TEST(NssCompiler, starting_conditional_leaves_its_result_for_the_caller) {
    EXPECT_EQ(1, run("int StartingConditional() { return 1; }").value);
    EXPECT_EQ(0, run("int StartingConditional() { return 0; }").value);
    EXPECT_EQ(42, run("int g = 40;\nint StartingConditional() { g += 2; return g; }").value);
    EXPECT_EQ(-1, run("void main() { }").value) << "a void main leaves nothing";
    EXPECT_EQ(-1, run("int g = 3;\nvoid main() { g++; }").value) << "not even with globals";
}

// Action parameters ----------------------------------------------------------------------------

TEST(NssCompiler, assign_command_runs_its_action_with_the_locals_of_the_caller) {
    auto r = run("void announce(int n, string s) { Out(n); OutS(s); }\nvoid main() { int x = 7; string t = \"hi\"; AssignCommand(OBJECT_SELF, announce(x + 1, t)); Out(99); }");
    EXPECT_EQ((Out {"8", "hi", "99"}), r.out);
}

TEST(NssCompiler, delay_command_captures_values_and_globals_at_the_time_of_the_call) {
    FakeRoutines routines;
    auto program = compileNss("test",
                              "int g = 5;\nvoid show(int a, int b, int c) { Out(a); Out(b); Out(c); }\n"
                              "void main() { int local = 1; DelayCommand(1.5, show(local, g, local + g)); local = 100; g = 200; Out(0); }",
                              routines);
    auto ctx = std::make_unique<ExecutionContext>();
    ctx->routines = &routines;
    VirtualMachine(program, std::move(ctx)).run();
    ASSERT_EQ(1u, routines.delayed.size());
    EXPECT_EQ((Out {"0"}), routines.out);
    routines.run(*routines.delayed[0]);
    // The arguments are evaluated when the action runs, from the saved copy of the
    // stack: the local keeps its value of the call, the global its saved value too.
    ASSERT_EQ(4u, routines.out.size());
    EXPECT_EQ("1", routines.out[1]);
    EXPECT_EQ("5", routines.out[2]);
    EXPECT_EQ("6", routines.out[3]);
}

TEST(NssCompiler, nested_actions_and_user_calls_inside_actions_work) {
    auto r = run("int twice(int n) { return n * 2; }\nvoid main() { int v = 4; AssignCommand(OBJECT_SELF, Out(twice(v))); AssignCommand(OBJECT_SELF, OutS(Cat3(\"a\", \"b\", \"c\"))); }");
    EXPECT_EQ((Out {"8", "abc"}), r.out);
}

// The NCS file -----------------------------------------------------------------------------------

TEST(NssCompiler, compiled_programs_survive_the_ncs_file_format) {
    const char *source =
        "int g = 2;\nfloat f = 1.5;\nstring s = \"a\";\n"
        "int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\n"
        "void main() { vector v = [1.0, 2.0, 3.0]; for (int i = 0; i < 3; i++) { g += i; } Out(g); Out(fib(8)); OutF(f * 2); OutS(s + \"b\"); OutV(v * 2.0); AssignCommand(OBJECT_SELF, Out(g)); }";
    FakeRoutines routines;
    auto direct = compileNss("test", source, routines);
    auto bytes = writeNcsBytes(*direct);
    ASSERT_GT(bytes.size(), 13u);
    EXPECT_EQ("NCS V1.0", std::string(bytes.begin(), bytes.begin() + 8));
    auto loaded = readNcsBytes("test", bytes);
    EXPECT_TRUE(sameProgram(*direct, *loaded));

    auto a = run(source, false);
    auto b = run(source, true);
    EXPECT_EQ(a.out, b.out);
    ASSERT_EQ(6u, a.out.size());
    EXPECT_EQ("5", a.out[0]);
    EXPECT_EQ("21", a.out[1]);
}

// Errors --------------------------------------------------------------------------------------------

namespace {

std::string errorOf(const std::string &source) {
    FakeRoutines routines;
    try {
        compileNss("test", source, routines);
    } catch (const NssError &e) {
        return e.what();
    }
    return "";
}

} // namespace

TEST(NssCompiler, reports_the_line_of_syntax_and_type_errors) {
    EXPECT_NE(std::string::npos, errorOf("void main() {\n  Out(1)\n}").find("line 3"));
    EXPECT_NE(std::string::npos, errorOf("void main() { Out(nothing); }").find("unknown name 'nothing'"));
    EXPECT_NE(std::string::npos, errorOf("void main() { Nope(1); }").find("unknown function 'Nope'"));
    EXPECT_NE(std::string::npos, errorOf("void main() { int a = \"s\"; }").find("cannot use string as int"));
    EXPECT_NE(std::string::npos, errorOf("void main() { string s = \"a\" - \"b\"; }").find("cannot apply '-'"));
    EXPECT_NE(std::string::npos, errorOf("void main() { Out(); Out(1, 2); }").find("takes at most 1"));
    EXPECT_NE(std::string::npos, errorOf("int f() { }\nvoid main() { return 1; }").find("returns no value"));
    EXPECT_NE(std::string::npos, errorOf("void main() { break; }").find("loop"));
    EXPECT_NE(std::string::npos, errorOf("int helper() { return 1; }").find("no 'main'"));
    EXPECT_NE(std::string::npos, errorOf("void main() { int a; int a; }").find("declared twice"));
    EXPECT_NE(std::string::npos, errorOf("void main() { switch (1) { } }").find("not supported"));
    EXPECT_NE(std::string::npos, errorOf("void main() { string s = \"open").find("unterminated string"));
    EXPECT_NE(std::string::npos, errorOf("void f(int a = 1) { }\nvoid main() { }").find("default argument"));
    EXPECT_NE(std::string::npos, errorOf("void main() { AssignCommand(OBJECT_SELF, Out(1) }").find("expected ')'"));
}
