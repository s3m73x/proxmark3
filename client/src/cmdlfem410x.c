//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>

// modified marshmellow
// modified Iceman, 2020
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// Low frequency EM4x commands
//-----------------------------------------------------------------------------

#include "cmdlfem410x.h"
#include "cmdlfem4x50.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdlib.h>
#include "fileutils.h"
#include "cmdparser.h"    // command_t
#include "comms.h"
#include "commonutil.h"
#include "common.h"
#include "util_posix.h"
#include "protocols.h"
#include "ui.h"
#include "proxgui.h"
#include "graph.h"
#include "cmddata.h"
#include "cmdlf.h"
#include "lfdemod.h"
#include "generator.h"
#include "cliparser.h"
#include "cmdhw.h"

static uint64_t g_em410xid = 0;

static int CmdHelp(const char *Cmd);
/* Read the ID of an EM410x tag.
 * Format:
 *   1111 1111 1           <-- standard non-repeatable header
 *   XXXX [row parity bit] <-- 10 rows of 5 bits for our 40 bit tag ID
 *   ....
 *   CCCC                  <-- each bit here is parity for the 10 bits above in corresponding column
 *   0                     <-- stop bit, end of tag
 */

// Construct the graph for emulating an EM410X tag
static void em410x_construct_emul_graph(uint8_t *uid, uint8_t clock) {

    // clear our graph
    ClearGraph(true);

    // write 16 zero bit sledge
    for (uint8_t i = 0; i < 20; i++)
        AppendGraph(false, clock, 0);

    // write 9 start bits
    for (uint8_t i = 0; i < 9; i++)
        AppendGraph(false, clock, 1);

    uint8_t bs[8], parity[8];
    memset(parity, 0, sizeof(parity));

    for (uint8_t i = 0; i < 5; i++) {

        for (uint8_t j = 0; j < 8; j++) {
            bs[j] = (uid[i] >> (7 - j) & 1);
        }
        PrintAndLogEx(DEBUG, "uid[%d] 0x%02x (%s)", i, uid[i], sprint_bin(bs, 4));

        for (uint8_t j = 0; j < 2; j++) {
            // append each bit
            AppendGraph(false, clock, bs[0 + (4 * j)]);
            AppendGraph(false, clock, bs[1 + (4 * j)]);
            AppendGraph(false, clock, bs[2 + (4 * j)]);
            AppendGraph(false, clock, bs[3 + (4 * j)]);

            // append parity bit
            AppendGraph(false, clock, bs[0 + (4 * j)] ^ bs[1 + (4 * j)] ^ bs[2 + (4 * j)] ^ bs[3 + (4 * j)]);

            // keep track of column parity
            parity[0] ^= bs[0 + (4 * j)];
            parity[1] ^= bs[1 + (4 * j)];
            parity[2] ^= bs[2 + (4 * j)];
            parity[3] ^= bs[3 + (4 * j)];
        }
    }

    // parity columns
    AppendGraph(false, clock, parity[0]);
    AppendGraph(false, clock, parity[1]);
    AppendGraph(false, clock, parity[2]);
    AppendGraph(false, clock, parity[3]);

    // stop bit
    AppendGraph(true, clock, 0);
}

//print 64 bit EM410x ID in multiple formats
void printEM410x(uint32_t hi, uint64_t id, bool verbose) {

    if (!id && !hi) return;

    uint64_t n = 1;
    uint64_t id2lo = 0;
    uint8_t m, i;
    for (m = 5; m > 0; m--) {
        for (i = 0; i < 8; i++) {
            id2lo = (id2lo << 1LL) | ((id & (n << (i + ((m - 1) * 8)))) >> (i + ((m - 1) * 8)));
        }
    }

    if (verbose == false) {

        if (hi) {
            PrintAndLogEx(SUCCESS, "EM 410x ID "_GREEN_("%06X%016" PRIX64), hi, id);
        } else {
            PrintAndLogEx(SUCCESS, "EM 410x ID "_GREEN_("%010" PRIX64), id);
        }
        return;
    }

    if (hi) {
        //output 88 bit em id
        PrintAndLogEx(SUCCESS, "EM 410x ID "_GREEN_("%06X%016" PRIX64), hi, id);
        PrintAndLogEx(SUCCESS, "EM410x XL ( RF/%d )", g_DemodClock);
    } else {
        //output 40 bit em id
        PrintAndLogEx(SUCCESS, "EM 410x ID "_GREEN_("%010" PRIX64), id);
        PrintAndLogEx(SUCCESS, "EM410x ( RF/%d )", g_DemodClock);
        PrintAndLogEx(INFO, "-------- " _CYAN_("Possible de-scramble patterns") " ---------");
        PrintAndLogEx(SUCCESS, "Unique TAG ID      : %010" PRIX64, id2lo);
        PrintAndLogEx(INFO, "HoneyWell IdentKey");
        PrintAndLogEx(SUCCESS, "    DEZ 8          : %08" PRIu64, id & 0xFFFFFF);
        PrintAndLogEx(SUCCESS, "    DEZ 10         : %010" PRIu64, id & 0xFFFFFFFF);
        PrintAndLogEx(SUCCESS, "    DEZ 5.5        : %05" PRIu64 ".%05" PRIu64, (id >> 16LL) & 0xFFFF, (id & 0xFFFF));
        PrintAndLogEx(SUCCESS, "    DEZ 3.5A       : %03" PRIu64 ".%05" PRIu64, (id >> 32ll), (id & 0xFFFF));
        PrintAndLogEx(SUCCESS, "    DEZ 3.5B       : %03" PRIu64 ".%05" PRIu64, (id & 0xFF000000) >> 24, (id & 0xFFFF));
        PrintAndLogEx(SUCCESS, "    DEZ 3.5C       : %03" PRIu64 ".%05" PRIu64, (id & 0xFF0000) >> 16, (id & 0xFFFF));
        PrintAndLogEx(SUCCESS, "    DEZ 14/IK2     : %014" PRIu64, id);
        PrintAndLogEx(SUCCESS, "    DEZ 15/IK3     : %015" PRIu64, id2lo);
        PrintAndLogEx(SUCCESS, "    DEZ 20/ZK      : %02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64 "%02" PRIu64,
                      (id2lo & 0xf000000000) >> 36,
                      (id2lo & 0x0f00000000) >> 32,
                      (id2lo & 0x00f0000000) >> 28,
                      (id2lo & 0x000f000000) >> 24,
                      (id2lo & 0x0000f00000) >> 20,
                      (id2lo & 0x00000f0000) >> 16,
                      (id2lo & 0x000000f000) >> 12,
                      (id2lo & 0x0000000f00) >> 8,
                      (id2lo & 0x00000000f0) >> 4,
                      (id2lo & 0x000000000f)
                     );
        PrintAndLogEx(INFO, "");

        uint64_t paxton = (((id >> 32) << 24) | (id & 0xffffff))  + 0x143e00;
        PrintAndLogEx(SUCCESS, "Other              : %05" PRIu64 "_%03" PRIu64 "_%08" PRIu64, (id & 0xFFFF), ((id >> 16LL) & 0xFF), (id & 0xFFFFFF));
        PrintAndLogEx(SUCCESS, "Pattern Paxton     : %" PRIu64 " [0x%" PRIX64 "]", paxton, paxton);

        uint32_t p1id = (id & 0xFFFFFF);
        uint8_t arr[32] = {0x00};
        int j = 23;
        for (int k = 0 ; k < 24; ++k, --j) {
            arr[k] = (p1id >> k) & 1;
        }

        uint32_t p1  = 0;

        p1 |= arr[23] << 21;
        p1 |= arr[22] << 23;
        p1 |= arr[21] << 20;
        p1 |= arr[20] << 22;

        p1 |= arr[19] << 18;
        p1 |= arr[18] << 16;
        p1 |= arr[17] << 19;
        p1 |= arr[16] << 17;

        p1 |= arr[15] << 13;
        p1 |= arr[14] << 15;
        p1 |= arr[13] << 12;
        p1 |= arr[12] << 14;

        p1 |= arr[11] << 6;
        p1 |= arr[10] << 2;
        p1 |= arr[9]  << 7;
        p1 |= arr[8]  << 1;

        p1 |= arr[7]  << 0;
        p1 |= arr[6]  << 8;
        p1 |= arr[5]  << 11;
        p1 |= arr[4]  << 3;

        p1 |= arr[3]  << 10;
        p1 |= arr[2]  << 4;
        p1 |= arr[1]  << 5;
        p1 |= arr[0]  << 9;
        PrintAndLogEx(SUCCESS, "Pattern 1          : %d [0x%X]", p1, p1);

        uint16_t sebury1 = id & 0xFFFF;
        uint8_t  sebury2 = (id >> 16) & 0x7F;
        uint32_t sebury3 = id & 0x7FFFFF;
        PrintAndLogEx(SUCCESS, "Pattern Sebury     : %d %d %d  [0x%X 0x%X 0x%X]", sebury1, sebury2, sebury3, sebury1, sebury2, sebury3);
        PrintAndLogEx(INFO, "------------------------------------------------");
    }
}
/* Read the ID of an EM410x tag.
 * Format:
 *   1111 1111 1           <-- standard non-repeatable header
 *   XXXX [row parity bit] <-- 10 rows of 5 bits for our 40 bit tag ID
 *   ....
 *   CCCC                  <-- each bit here is parity for the 10 bits above in corresponding column
 *   0                     <-- stop bit, end of tag
 */
int AskEm410xDecode(bool verbose, uint32_t *hi, uint64_t *lo) {
    size_t idx = 0;
    uint8_t bits[512] = {0};
    size_t size = sizeof(bits);
    if (!getDemodBuff(bits, &size)) {
        PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x problem during copy from ASK demod");
        return PM3_ESOFT;
    }

    int ans = Em410xDecode(bits, &size, &idx, hi, lo);
    if (ans < 0) {

        if (ans == -2)
            PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x not enough samples after demod");
        else if (ans == -4)
            PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x preamble not found");
        else if (ans == -5)
            PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x Size not correct: %zu", size);
        else if (ans == -6)
            PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x parity failed");

        return PM3_ESOFT;
    }
    if (!lo && !hi) {
        PrintAndLogEx(DEBUG, "DEBUG: Error - Em410x decoded to all zeros");
        return PM3_ESOFT;
    }

    //set GraphBuffer for clone or sim command
    setDemodBuff(DemodBuffer, (size == 40) ? 64 : 128, idx + 1);
    setClockGrid(g_DemodClock, g_DemodStartIdx + ((idx + 1)*g_DemodClock));

    PrintAndLogEx(DEBUG, "DEBUG: Em410x idx: %zu, Len: %zu, Printing Demod Buffer:", idx, size);
    if (g_debugMode) {
        printDemodBuff(0, false, false, true);
    }

    printEM410x(*hi, *lo, verbose);
    g_em410xid = *lo;
    return PM3_SUCCESS;
}

int AskEm410xDemod(int clk, int invert, int maxErr, size_t maxLen, bool amplify, uint32_t *hi, uint64_t *lo, bool verbose) {
    bool st = true;

    // em410x simulation etc uses 0/1 as signal data. This must be converted in order to demod it back again
    if (isGraphBitstream()) {
        convertGraphFromBitstream();
    }
    if (ASKDemod_ext(clk, invert, maxErr, maxLen, amplify, false, false, 1, &st) != PM3_SUCCESS)
        return PM3_ESOFT;
    return AskEm410xDecode(verbose, hi, lo);
}

// this read loops on device side.
// uses the demod in lfops.c
static int CmdEM410xWatch(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x watch",
                  "Enables Electro Marine (EM) compatible reader mode printing details of scanned tags.\n"
                  "Run until the button is pressed or another USB command is issued.",
                  "lf em 410x watch"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);
    CLIParserFree(ctx);

    PrintAndLogEx(SUCCESS, "Watching for EM410x cards - place tag on antenna");
    PrintAndLogEx(INFO, "Press pm3-button to stop reading cards");
    clearCommandBuffer();
    SendCommandNG(CMD_LF_EM410X_WATCH, NULL, 0);
    PacketResponseNG resp;
    WaitForResponse(CMD_LF_EM410X_WATCH, &resp);
    PrintAndLogEx(INFO, "Done");
    return resp.status;
}

//by marshmellow
//takes 3 arguments - clock, invert and maxErr as integers
//attempts to demodulate ask while decoding manchester
//prints binary found and saves in graphbuffer for further commands
int demodEM410x(bool verbose) {
    (void) verbose; // unused so far
    uint32_t hi = 0;
    uint64_t lo = 0;
    return AskEm410xDemod(0, 0, 100, 0, false, &hi, &lo, true);
}

static int CmdEM410xDemod(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x demod",
                  "Try to find EM 410x preamble, if found decode / descramble data",
                  "lf em 410x demod                      -> demod an EM410x Tag ID from GraphBuffer\n"
                  "lf em 410x demod --clk 32             -> demod an EM410x Tag ID from GraphBuffer using a clock of RF/32\n"
                  "lf em 410x demod --clk 32 -i          -> demod an EM410x Tag ID from GraphBuffer using a clock of RF/32 and inverting data\n"
                  "lf em 410x demod -i                   -> demod an EM410x Tag ID from GraphBuffer while inverting data\n"
                  "lf em 410x demod --clk 64 -i --err 0  -> demod an EM410x Tag ID from GraphBuffer using a clock of RF/64 and inverting data and allowing 0 demod errors"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_0(NULL, "clk", "<dec>", "optional - clock (default autodetect)"),
        arg_u64_0(NULL, "err", "<dec>", "optional - maximum allowed errors (default 100)"),
        arg_u64_0(NULL, "len", "<dec>", "optional - maximum length"),
        arg_lit0("i", "invert", "optional - invert output"),
        arg_lit0("a", "amp", "optional - amplify signal"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    int clk = arg_get_u32_def(ctx, 1, 0);
    int max_err = arg_get_u32_def(ctx, 2, 100);
    size_t max_len = arg_get_u32_def(ctx, 3, 0);
    bool invert = arg_get_lit(ctx, 4);
    bool amplify = arg_get_lit(ctx, 5);
    CLIParserFree(ctx);

    uint32_t hi = 0;
    uint64_t lo = 0;
    if (AskEm410xDemod(clk, invert, max_err, max_len, amplify, &hi, &lo, true) != PM3_SUCCESS)
        return PM3_ESOFT;

    return PM3_SUCCESS;
}

// this read is the "normal" read,  which download lf signal and tries to demod here.
static int CmdEM410xReader(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x reader",
                  "read EM 410x tag",
                  "lf em 410x reader                      -> reader\n"
                  "lf em 410x reader -@                   -> continuous reader mode\n"
                  "lf em 410x reader --clk 32             -> reader using a clock of RF/32\n"
                  "lf em 410x reader --clk 32 -i          -> reader using a clock of RF/32 and inverting data\n"
                  "lf em 410x reader -i                   -> reader while inverting data\n"
                  "lf em 410x reader --clk 64 -i --err 0  -> reader using a clock of RF/64 and inverting data and allowing 0 demod errors"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_0(NULL, "clk", "<dec>", "optional - clock (default autodetect)"),
        arg_u64_0(NULL, "err", "<dec>", "optional - maximum allowed errors (default 100)"),
        arg_u64_0(NULL, "len", "<dec>", "optional - maximum length"),
        arg_lit0("i", "invert", "optional - invert output"),
        arg_lit0("a", "amp", "optional - amplify signal"),
        arg_lit0("@", NULL, "optional - continuous reader mode"),
        arg_lit0("v", "verbose", "verbose output"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    int clk = arg_get_u32_def(ctx, 1, 0);
    int max_err = arg_get_u32_def(ctx, 2, 100);
    size_t max_len = arg_get_u32_def(ctx, 3, 0);
    bool invert = arg_get_lit(ctx, 4);
    bool amplify = arg_get_lit(ctx, 5);
    bool cm = arg_get_lit(ctx, 6);
    bool verbose = arg_get_lit(ctx, 7);
    CLIParserFree(ctx);

    if (cm) {
        PrintAndLogEx(INFO, "Press " _GREEN_("<Enter>") " to exit");
    }

    do {
        uint32_t hi = 0;
        uint64_t lo = 0;
        lf_read(false, 12288);
        AskEm410xDemod(clk, invert, max_err, max_len, amplify, &hi, &lo, verbose);
    } while (cm && !kbd_enter_pressed());

    return PM3_SUCCESS;
}

// emulate an EM410X tag
static int CmdEM410xSim(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x sim",
                  "Enables simulation of EM 410x card.\n"
                  "Simulation runs until the button is pressed or another USB command is issued.",
                  "lf em 410x sim --id 0F0368568B\n"
                  "lf em 410x sim --id 0F0368568B --clk 32"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_0(NULL, "clk", "<dec>", "optional - clock [32|64] (default 64)"),
        arg_str1("i", "id", "<hex>", "ID number (5 hex bytes)"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    // clock is 64 in EM410x tags
    int clk = arg_get_u32_def(ctx, 1, 64);
    int uid_len = 0;
    uint8_t uid[5] = {0};
    CLIGetHexWithReturn(ctx, 2, uid, &uid_len);
    CLIParserFree(ctx);

    if (uid_len != 5) {
        PrintAndLogEx(FAILED, "UID must include 5 hex bytes (%u)", uid_len);
        return PM3_EINVARG;
    }

    PrintAndLogEx(SUCCESS, "Starting simulating UID "_YELLOW_("%s")" clock: "_YELLOW_("%d"), sprint_hex_inrow(uid, sizeof(uid)), clk);
    PrintAndLogEx(SUCCESS, "Press pm3-button to abort simulation");

    em410x_construct_emul_graph(uid, clk);

    CmdLFSim("0"); // 240 start_gap.
    return PM3_SUCCESS;
}

static int CmdEM410xBrute(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x brute",
                  "bruteforcing by emulating EM 410x tag",
                  "lf em 410x brute -f ids.txt\n"
                  "lf em 410x brute -f ids.txt --clk 32\n"
                  "lf em 410x brute -f ids.txt --delay 3000\n"
                  "lf em 410x brute -f ids.txt --delay 3000 --clk 32\n"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_1(NULL, "clk", "<dec>", "optional - clock [32|64] (default 64)"),
        arg_u64_1(NULL, "delay", "<dec>", "optional - pause delay in milliseconds between UIDs simulation (default 1000ms)"),
        arg_str1("f", "file", "<hex>", "file with UIDs in HEX format, one per line"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    // clock default 64 in EM410x
    uint32_t clk = arg_get_u32_def(ctx, 1, 64);

    // default pause time: 1 second
    uint32_t delay = arg_get_u32_def(ctx, 2, 1000);

    int fnlen = 0;
    char filename[FILE_PATH_SIZE] = {0};
    CLIParamStrToBuf(arg_get_str(ctx, 3), (uint8_t *)filename, FILE_PATH_SIZE, &fnlen);
    CLIParserFree(ctx);

    if (fnlen == 0) {
        PrintAndLogEx(ERR, "Error: Please specify a filename");
        return PM3_EINVARG;
    }

    uint32_t uidcnt = 0;
    uint8_t stUidBlock = 20;
    uint8_t *p = NULL;
    uint8_t uid[5] = {0x00};

    // open file
    FILE *f = NULL;
    if ((f = fopen(filename, "r")) == NULL) {
        PrintAndLogEx(ERR, "Error: Could not open UIDs file ["_YELLOW_("%s")"]", filename);
        return PM3_EFILE;
    }

    // allocate mem for file contents
    uint8_t *uidblock = calloc(stUidBlock, 5);
    if (uidblock == NULL) {
        fclose(f);
        PrintAndLogEx(ERR, "Error: can't allocate memory");
        return PM3_EMALLOC;
    }

    // read file into memory
    char buf[11];

    while (fgets(buf, sizeof(buf), f)) {
        if (strlen(buf) < 10 || buf[9] == '\n') continue;
        while (fgetc(f) != '\n' && !feof(f));  //goto next line

        //The line start with # is comment, skip
        if (buf[0] == '#') continue;

        if (param_gethex(buf, 0, uid, 10)) {
            PrintAndLogEx(FAILED, "UIDs must include 10 HEX symbols");
            free(uidblock);
            fclose(f);
            return PM3_ESOFT;
        }

        buf[10] = 0;

        if (stUidBlock - uidcnt < 2) {
            p = realloc(uidblock, 5 * (stUidBlock += 10));
            if (!p) {
                PrintAndLogEx(WARNING, "Cannot allocate memory for UIDs");
                free(uidblock);
                fclose(f);
                return PM3_ESOFT;
            }
            uidblock = p;
        }
        memset(uidblock + 5 * uidcnt, 0, 5);
        num_to_bytes(strtoll(buf, NULL, 16), 5, uidblock + 5 * uidcnt);
        uidcnt++;
        memset(buf, 0, sizeof(buf));
    }
    fclose(f);

    if (uidcnt == 0) {
        PrintAndLogEx(FAILED, "No UIDs found in file");
        free(uidblock);
        return PM3_ESOFT;
    }

    PrintAndLogEx(SUCCESS, "Loaded "_YELLOW_("%d")" UIDs from "_YELLOW_("%s")", pause delay:"_YELLOW_("%d")" ms", uidcnt, filename, delay);

    // loop
    uint8_t testuid[5];
    for (uint32_t c = 0; c < uidcnt; ++c) {
        if (kbd_enter_pressed()) {
            PrintAndLogEx(WARNING, "\nAborted via keyboard!\n");
            free(uidblock);
            return PM3_EOPABORTED;
        }

        memcpy(testuid, uidblock + 5 * c, 5);
        PrintAndLogEx(INFO, "Bruteforce %d / %d: simulating UID " _YELLOW_("%s")
                      , c + 1
                      , uidcnt
                      , sprint_hex_inrow(testuid, sizeof(testuid))
                     );

        em410x_construct_emul_graph(testuid, clk);

        CmdLFSim("0"); //240 start_gap.

        msleep(delay);
    }
    free(uidblock);
    return PM3_SUCCESS;
}

//currently only supports manchester modulations
static int CmdEM410xSpoof(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x spoof",
                  "Watch 'nd Spoof, activates reader\n"
                  "Waits until a EM 410x tag gets presented then Proxmark3 starts simulating the found UID",
                  "lf em 410x spoof"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);
    CLIParserFree(ctx);

    // loops if the captured ID was in XL-format.
    CmdEM410xReader("-@");
    PrintAndLogEx(SUCCESS, "# Replaying captured ID: "_YELLOW_("%010" PRIx64), g_em410xid);
    CmdLFaskSim("");
    return PM3_SUCCESS;
}

static int CmdEM410xClone(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "lf em 410x clone",
                  "Writes EM410x ID to a T55x7 or Q5/T5555 tag",
                  "lf em 410x clone --id 0F0368568B        -> write id to T55x7 tag\n"
                  "lf em 410x clone --id 0F0368568B --q5   -> write id to Q5/T5555 tag"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_u64_0(NULL, "clk", "<dec>", "optional - clock <16|32|40|64> (default 64)"),
        arg_str1("u", "uid", "<hex>", "ID number (5 hex bytes)"),
        arg_lit0(NULL, "q5", "optional - specify writing to Q5/T5555 tag"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    // clock default 64 in EM410x
    uint32_t clk = arg_get_u32_def(ctx, 1, 64);
    int uid_len = 0;
    uint8_t uid[5] = {0};
    CLIGetHexWithReturn(ctx, 2, uid, &uid_len);
    bool q5 = arg_get_lit(ctx, 3);
    CLIParserFree(ctx);

    uint64_t id = bytes_to_num(uid, uid_len);

    // Allowed clock rates: 16, 32, 40 and 64
    if ((clk != 16) && (clk != 32) && (clk != 64) && (clk != 40)) {
        PrintAndLogEx(FAILED, "supported clock rates are " _YELLOW_("16, 32, 40, 64") "  got " _RED_("%d") "\n", clk);
        return PM3_EINVARG;
    }

    char cardtype[16] = {"T55x7"};
    if (q5) {
        snprintf(cardtype, sizeof(cardtype), "Q5/T5555");
    }

    PrintAndLogEx(SUCCESS, "Preparing to clone EM4102 to " _YELLOW_("%s") " tag with ID " _GREEN_("%010" PRIX64) " (RF/%d)", cardtype, id, clk);
    // NOTE: We really should pass the clock in as a separate argument, but to
    //   provide for backwards-compatibility for older firmware, and to avoid
    //   having to add another argument to CMD_LF_EM410X_WRITE, we just store
    //   the clock rate in bits 8-15 of the card value

    struct {
        uint8_t card;
        uint8_t clock;
        uint32_t high;
        uint32_t low;
    } PACKED params;

    params.card = (q5) ? 0 : 1;
    params.clock = clk;
    params.high = (uint32_t)(id >> 32);
    params.low = (uint32_t)id;

    clearCommandBuffer();
    SendCommandNG(CMD_LF_EM410X_WRITE, (uint8_t *)&params, sizeof(params));

    PacketResponseNG resp;
    WaitForResponse(CMD_LF_EM410X_WRITE, &resp);
    switch (resp.status) {
        case PM3_SUCCESS: {
            PrintAndLogEx(SUCCESS, "Done");
            PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`lf em 410x reader`") " to verify");
            break;
        }
        default: {
            PrintAndLogEx(WARNING, "Something went wrong");
            break;
        }
    }
    return resp.status;
}

static command_t CommandTable[] = {
    {"help",        CmdHelp,      AlwaysAvailable, "This help"},
    //{"demod",  CmdEMdemodASK,    IfPm3Lf,         "Extract ID from EM410x tag on antenna)"},
    {"demod",  CmdEM410xDemod,    AlwaysAvailable, "demodulate a EM410x tag from the GraphBuffer"},
    {"reader", CmdEM410xReader,   IfPm3Lf,         "attempt to read and extract tag data"},
    {"sim",    CmdEM410xSim,      IfPm3Lf,         "simulate EM410x tag"},
    {"brute",  CmdEM410xBrute,    IfPm3Lf,         "reader bruteforce attack by simulating EM410x tags"},
    {"watch",  CmdEM410xWatch,    IfPm3Lf,         "watches for EM410x 125/134 kHz tags (option 'h' for 134)"},
    {"spoof",  CmdEM410xSpoof,    IfPm3Lf,         "watches for EM410x 125/134 kHz tags, and replays them. (option 'h' for 134)" },
    {"clone",  CmdEM410xClone,    IfPm3Lf,         "write EM410x UID to T55x7 or Q5/T5555 tag"},
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdLFEM410X(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}
