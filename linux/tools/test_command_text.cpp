// Unit tests for CommandText.h (feedback#22). Build:
//   g++ -std=c++14 -I linux/service/source/Tools -o test_command_text linux/tools/test_command_text.cpp
#include "CommandText.h"

#include <cstdio>

using chatrobot::isCreatorOnlyCommand;
using chatrobot::isSlashCommand;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) { g_pass++; } else { g_fail++; \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    // Commands: never stored or relayed.
    CHECK(isSlashCommand("/help"));
    CHECK(isSlashCommand("/h"));
    CHECK(isSlashCommand("/friends list"));           // Weili's 11-24 message
    CHECK(isSlashCommand("/List"));
    CHECK(isSlashCommand("  /list"));
    CHECK(isSlashCommand("\n\t/ens show"));
    CHECK(isSlashCommand("/usr/bin is where it lives")); // starts like a command: it is one
    CHECK(isSlashCommand("/x [2026-11-24 10:00:00]"));

    // Chat: relayed as today.
    CHECK(!isSlashCommand(""));
    CHECK(!isSlashCommand("/"));
    CHECK(!isSlashCommand("  /  "));
    CHECK(!isSlashCommand("//"));
    CHECK(!isSlashCommand("// comment"));
    CHECK(!isSlashCommand("/ help"));
    CHECK(!isSlashCommand("/1 is a number"));
    CHECK(!isSlashCommand("/:)"));
    CHECK(!isSlashCommand(":/"));
    CHECK(!isSlashCommand("see /usr/local/bin"));
    CHECK(!isSlashCommand("and/or"));
    CHECK(!isSlashCommand("https://app.beagle.chat"));
    CHECK(!isSlashCommand("\xef\xbc\x8fhelp"));          // fullwidth slash
    CHECK(!isSlashCommand("/\xe5\xb8\xae\xe5\x8a\xa9"));  // "/" + non-ASCII letter
    CHECK(!isSlashCommand("CGR1 {\"text\":\"/help\"}"));

    CHECK(isCreatorOnlyCommand("b") && isCreatorOnlyCommand("block"));
    CHECK(isCreatorOnlyCommand("d") && isCreatorOnlyCommand("del"));
    CHECK(isCreatorOnlyCommand("u") && isCreatorOnlyCommand("update"));
    CHECK(isCreatorOnlyCommand("e") && isCreatorOnlyCommand("exit"));
    CHECK(isCreatorOnlyCommand("pending") && isCreatorOnlyCommand("approve") && isCreatorOnlyCommand("reject") && isCreatorOnlyCommand("join"));
    CHECK(!isCreatorOnlyCommand("help") && !isCreatorOnlyCommand("list") && !isCreatorOnlyCommand("l"));
    CHECK(!isCreatorOnlyCommand("address") && !isCreatorOnlyCommand("agent") && !isCreatorOnlyCommand("ub"));

    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
