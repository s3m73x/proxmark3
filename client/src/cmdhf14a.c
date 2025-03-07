//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>, Hagen Fritsch
// 2011, 2017 - 2019 Merlok
// 2014, Peter Fillmore
// 2015, 2016, 2017 Iceman
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// High frequency ISO14443A commands
//-----------------------------------------------------------------------------
#include "cmdhf14a.h"
#include <ctype.h>
#include <string.h>
#include "cmdparser.h"    // command_t
#include "commonutil.h"   // ARRAYLEN
#include "comms.h"        // clearCommandBuffer
#include "cmdtrace.h"
#include "cliparser.h"
#include "cmdhfmf.h"
#include "cmdhfmfu.h"
#include "emv/emvcore.h"
#include "ui.h"
#include "crc16.h"
#include "util_posix.h"  // msclock
#include "aidsearch.h"
#include "cmdhf.h"       // handle HF plot
#include "cliparser.h"
#include "protocols.h"     // definitions of ISO14A/7816 protocol, MAGIC_GEN_1A
#include "emv/apduinfo.h"  // GetAPDUCodeDescription

bool APDUInFramingEnable = true;

static int CmdHelp(const char *Cmd);
static int waitCmd(bool i_select, uint32_t timeout);

static const manufactureName manufactureMapping[] = {
    // ID,  "Vendor Country"
    { 0x01, "Motorola UK" },
    { 0x02, "ST Microelectronics SA France" },
    { 0x03, "Hitachi, Ltd Japan" },
    { 0x04, "NXP Semiconductors Germany" },
    { 0x05, "Infineon Technologies AG Germany" },
    { 0x06, "Cylink USA" },
    { 0x07, "Texas Instrument France" },
    { 0x08, "Fujitsu Limited Japan" },
    { 0x09, "Matsushita Electronics Corporation, Semiconductor Company Japan" },
    { 0x0A, "NEC Japan" },
    { 0x0B, "Oki Electric Industry Co. Ltd Japan" },
    { 0x0C, "Toshiba Corp. Japan" },
    { 0x0D, "Mitsubishi Electric Corp. Japan" },
    { 0x0E, "Samsung Electronics Co. Ltd Korea" },
    { 0x0F, "Hynix / Hyundai, Korea" },
    { 0x10, "LG-Semiconductors Co. Ltd Korea" },
    { 0x11, "Emosyn-EM Microelectronics USA" },
    { 0x12, "INSIDE Technology France" },
    { 0x13, "ORGA Kartensysteme GmbH Germany" },
    { 0x14, "SHARP Corporation Japan" },
    { 0x15, "ATMEL France" },
    { 0x16, "EM Microelectronic-Marin SA Switzerland" },
    { 0x17, "KSW Microtec GmbH Germany" },
    { 0x18, "ZMD AG Germany" },
    { 0x19, "XICOR, Inc. USA" },
    { 0x1A, "Sony Corporation Japan" },
    { 0x1B, "Malaysia Microelectronic Solutions Sdn. Bhd Malaysia" },
    { 0x1C, "Emosyn USA" },
    { 0x1D, "Shanghai Fudan Microelectronics Co. Ltd. P.R. China" },
    { 0x1E, "Magellan Technology Pty Limited Australia" },
    { 0x1F, "Melexis NV BO Switzerland" },
    { 0x20, "Renesas Technology Corp. Japan" },
    { 0x21, "TAGSYS France" },
    { 0x22, "Transcore USA" },
    { 0x23, "Shanghai belling corp., ltd. China" },
    { 0x24, "Masktech Germany Gmbh Germany" },
    { 0x25, "Innovision Research and Technology Plc UK" },
    { 0x26, "Hitachi ULSI Systems Co., Ltd. Japan" },
    { 0x27, "Cypak AB Sweden" },
    { 0x28, "Ricoh Japan" },
    { 0x29, "ASK France" },
    { 0x2A, "Unicore Microsystems, LLC Russian Federation" },
    { 0x2B, "Dallas Semiconductor/Maxim USA" },
    { 0x2C, "Impinj, Inc. USA" },
    { 0x2D, "RightPlug Alliance USA" },
    { 0x2E, "Broadcom Corporation USA" },
    { 0x2F, "MStar Semiconductor, Inc Taiwan, ROC" },
    { 0x30, "BeeDar Technology Inc. USA" },
    { 0x31, "RFIDsec Denmark" },
    { 0x32, "Schweizer Electronic AG Germany" },
    { 0x33, "AMIC Technology Corp Taiwan" },
    { 0x34, "Mikron JSC Russia" },
    { 0x35, "Fraunhofer Institute for Photonic Microsystems Germany" },
    { 0x36, "IDS Microchip AG Switzerland" },
    { 0x37, "Thinfilm - Kovio USA" },
    { 0x38, "HMT Microelectronic Ltd Switzerland" },
    { 0x39, "Silicon Craft Technology Thailand" },
    { 0x3A, "Advanced Film Device Inc. Japan" },
    { 0x3B, "Nitecrest Ltd UK" },
    { 0x3C, "Verayo Inc. USA" },
    { 0x3D, "HID Global USA" },
    { 0x3E, "Productivity Engineering Gmbh Germany" },
    { 0x3F, "Austriamicrosystems AG (reserved) Austria" },
    { 0x40, "Gemalto SA France" },
    { 0x41, "Renesas Electronics Corporation Japan" },
    { 0x42, "3Alogics Inc Korea" },
    { 0x43, "Top TroniQ Asia Limited Hong Kong" },
    { 0x44, "Gentag Inc. USA" },
    { 0x45, "Invengo Information Technology Co.Ltd China" },
    { 0x46, "Guangzhou Sysur Microelectronics, Inc China" },
    { 0x47, "CEITEC S.A. Brazil" },
    { 0x48, "Shanghai Quanray Electronics Co. Ltd. China" },
    { 0x49, "MediaTek Inc Taiwan" },
    { 0x4A, "Angstrem PJSC Russia" },
    { 0x4B, "Celisic Semiconductor (Hong Kong) Limited China" },
    { 0x4C, "LEGIC Identsystems AG Switzerland" },
    { 0x4D, "Balluff GmbH Germany" },
    { 0x4E, "Oberthur Technologies France" },
    { 0x4F, "Silterra Malaysia Sdn. Bhd. Malaysia" },
    { 0x50, "DELTA Danish Electronics, Light & Acoustics Denmark" },
    { 0x51, "Giesecke & Devrient GmbH Germany" },
    { 0x52, "Shenzhen China Vision Microelectronics Co., Ltd. China" },
    { 0x53, "Shanghai Feiju Microelectronics Co. Ltd. China" },
    { 0x54, "Intel Corporation USA" },
    { 0x55, "Microsensys GmbH Germany" },
    { 0x56, "Sonix Technology Co., Ltd. Taiwan" },
    { 0x57, "Qualcomm Technologies Inc USA" },
    { 0x58, "Realtek Semiconductor Corp Taiwan" },
    { 0x59, "Freevision Technologies Co. Ltd China" },
    { 0x5A, "Giantec Semiconductor Inc. China" },
    { 0x5B, "JSC Angstrem-T Russia" },
    { 0x5C, "STARCHIP France" },
    { 0x5D, "SPIRTECH France" },
    { 0x5E, "GANTNER Electronic GmbH Austria" },
    { 0x5F, "Nordic Semiconductor Norway" },
    { 0x60, "Verisiti Inc USA" },
    { 0x61, "Wearlinks Technology Inc. China" },
    { 0x62, "Userstar Information Systems Co., Ltd Taiwan" },
    { 0x63, "Pragmatic Printing Ltd. UK" },
    { 0x64, "Associacao do Laboratorio de Sistemas Integraveis Tecnologico - LSI-TEC Brazil" },
    { 0x65, "Tendyron Corporation China" },
    { 0x66, "MUTO Smart Co., Ltd. Korea" },
    { 0x67, "ON Semiconductor USA" },
    { 0x68, "TUBITAK BILGEM Turkey" },
    { 0x69, "Huada Semiconductor Co., Ltd China" },
    { 0x6A, "SEVENEY France" },
    { 0x6B, "ISSM France" },
    { 0x6C, "Wisesec Ltd Israel" },
    { 0x7C, "DB HiTek Co Ltd Korea" },
    { 0x7D, "SATO Vicinity Australia" },
    { 0x7E, "Holtek Taiwan" },
    { 0x00, "no tag-info available" } // must be the last entry
};

// get a product description based on the UID
//  uid[8] tag uid
// returns description of the best match
const char *getTagInfo(uint8_t uid) {

    int i;

    for (i = 0; i < ARRAYLEN(manufactureMapping); ++i)
        if (uid == manufactureMapping[i].uid)
            return manufactureMapping[i].desc;

    //No match, return default
    return manufactureMapping[ARRAYLEN(manufactureMapping) - 1].desc;
}

// iso14a apdu input frame length
static uint16_t frameLength = 0;
uint16_t atsFSC[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};

static int usage_hf_14a_config(void) {
    PrintAndLogEx(NORMAL, "Usage: hf 14a config [a 0|1|2] [b 0|1|2] [2 0|1|2] [3 0|1|2]");
    PrintAndLogEx(NORMAL, "\nOptions:");
    PrintAndLogEx(NORMAL, "       h                 This help");
    PrintAndLogEx(NORMAL, "       a 0|1|2           ATQA<>anticollision: 0=follow standard 1=execute anticol 2=skip anticol");
    PrintAndLogEx(NORMAL, "       b 0|1|2           BCC:                 0=follow standard 1=use fixed BCC   2=use card BCC");
    PrintAndLogEx(NORMAL, "       2 0|1|2           SAK<>CL2:            0=follow standard 1=execute CL2     2=skip CL2");
    PrintAndLogEx(NORMAL, "       3 0|1|2           SAK<>CL3:            0=follow standard 1=execute CL3     2=skip CL3");
    PrintAndLogEx(NORMAL, "       r 0|1|2           SAK<>ATS:            0=follow standard 1=execute RATS    2=skip RATS");
    PrintAndLogEx(NORMAL, "\nExamples:");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config       ")"     Print current configuration");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1   ")"     Force execution of anticollision");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0   ")"     Restore ATQA interpretation");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config b 1   ")"     Force fix of bad BCC in anticollision");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config b 0   ")"     Restore BCC check");
    PrintAndLogEx(NORMAL, "\nExamples to revive Gen2/DirectWrite magic cards failing at anticollision:");
    PrintAndLogEx(NORMAL, _CYAN_("    MFC 1k 4b UID")":");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1 b 2 2 2 r 2"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf mf wrbl 0 A FFFFFFFFFFFF 11223344440804006263646566676869"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0 b 0 2 0 r 0"));
    PrintAndLogEx(NORMAL, _CYAN_("    MFC 4k 4b UID")":");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1 b 2 2 2 r 2"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf mf wrbl 0 A FFFFFFFFFFFF 11223344441802006263646566676869"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0 b 0 2 0 r 0"));
    PrintAndLogEx(NORMAL, _CYAN_("    MFC 1k 7b UID")":");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1 b 2 2 1 3 2 r 2"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf mf wrbl 0 A FFFFFFFFFFFF 04112233445566084400626364656667"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0 b 0 2 0 3 0 r 0"));
    PrintAndLogEx(NORMAL, _CYAN_("    MFC 4k 7b UID")":");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1 b 2 2 1 3 2 r 2"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf mf wrbl 0 A FFFFFFFFFFFF 04112233445566184200626364656667"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0 b 0 2 0 3 0 r 0"));
    PrintAndLogEx(NORMAL, _CYAN_("    MFUL ")"/" _CYAN_(" MFUL EV1 ")"/" _CYAN_(" MFULC")":");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 1 b 2 2 1 3 2 r 2"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf mfu setuid 04112233445566"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a config a 0 b 0 2 0 3 0 r 0"));
    return PM3_SUCCESS;
}

static int usage_hf_14a_sim(void) {
    PrintAndLogEx(NORMAL, "\n Emulating ISO/IEC 14443 type A tag with 4,7 or 10 byte UID\n");
    PrintAndLogEx(NORMAL, "Usage: hf 14a sim [h] t <type> u <uid> [n <numreads>] [x] [e] [v]");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h     : This help");
    PrintAndLogEx(NORMAL, "    t     : 1 = MIFARE Classic 1k");
    PrintAndLogEx(NORMAL, "            2 = MIFARE Ultralight");
    PrintAndLogEx(NORMAL, "            3 = MIFARE Desfire");
    PrintAndLogEx(NORMAL, "            4 = ISO/IEC 14443-4");
    PrintAndLogEx(NORMAL, "            5 = MIFARE Tnp3xxx");
    PrintAndLogEx(NORMAL, "            6 = MIFARE Mini");
    PrintAndLogEx(NORMAL, "            7 = AMIIBO (NTAG 215),  pack 0x8080");
    PrintAndLogEx(NORMAL, "            8 = MIFARE Classic 4k");
    PrintAndLogEx(NORMAL, "            9 = FM11RF005SH Shanghai Metro");
    PrintAndLogEx(NORMAL, "           10 = JCOP 31/41 Rothult");
    PrintAndLogEx(NORMAL, "    u     : 4, 7 or 10 byte UID");
    PrintAndLogEx(NORMAL, "    n     : (Optional) Exit simulation after <numreads> blocks have been read by reader. 0 = infinite");
    PrintAndLogEx(NORMAL, "    x     : (Optional) Performs the 'reader attack', nr/ar attack against a reader");
    PrintAndLogEx(NORMAL, "    e     : (Optional) Fill simulator keys from found keys");
    PrintAndLogEx(NORMAL, "    v     : (Optional) Verbose");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a sim t 1 u 11223344 x"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a sim t 1 u 11223344"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a sim t 1 u 11223344556677"));
    PrintAndLogEx(NORMAL, _YELLOW_("          hf 14a sim t 1 u 112233445566778899AA"));
    return PM3_SUCCESS;
}

static int CmdHF14AList(const char *Cmd) {
    char args[128] = {0};
    if (strlen(Cmd) == 0) {
        snprintf(args, sizeof(args), "-t 14a");
    } else {
        strncpy(args, Cmd, sizeof(args) - 1);
    }
    return CmdTraceList(args);
}

int hf14a_getconfig(hf14a_config *config) {
    if (!session.pm3_present) return PM3_ENOTTY;

    if (config == NULL)
        return PM3_EINVARG;

    clearCommandBuffer();

    SendCommandNG(CMD_HF_ISO14443A_GET_CONFIG, NULL, 0);
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_HF_ISO14443A_GET_CONFIG, &resp, 2000)) {
        PrintAndLogEx(WARNING, "command execution time out");
        return PM3_ETIMEOUT;
    }
    memcpy(config, resp.data.asBytes, sizeof(hf14a_config));
    return PM3_SUCCESS;
}

int hf14a_setconfig(hf14a_config *config) {
    if (!session.pm3_present) return PM3_ENOTTY;

    clearCommandBuffer();
    if (config != NULL)
        SendCommandNG(CMD_HF_ISO14443A_SET_CONFIG, (uint8_t *)config, sizeof(hf14a_config));
    else
        SendCommandNG(CMD_HF_ISO14443A_PRINT_CONFIG, NULL, 0);

    return PM3_SUCCESS;
}

static int CmdHf14AConfig(const char *Cmd) {

    if (!session.pm3_present) return PM3_ENOTTY;

    // if called with no params, just print the device config
    if (strlen(Cmd) == 0) {
        return hf14a_setconfig(NULL);
    }

    hf14a_config config = {
        .forceanticol = -1,
        .forcebcc = -1,
        .forcecl2 = -1,
        .forcecl3 = -1,
        .forcerats = -1
    };

    bool errors = false;
    uint8_t cmdp = 0;
    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (param_getchar(Cmd, cmdp)) {
            case 'h':
                return usage_hf_14a_config();
            case 'a':
                switch (param_getchar(Cmd, cmdp + 1)) {
                    case '0':
                        config.forceanticol = 0;
                        break;
                    case '1':
                        config.forceanticol = 1;
                        break;
                    case '2':
                        config.forceanticol = 2;
                        break;
                    default:
                        PrintAndLogEx(WARNING, "Unknown value '%c'", param_getchar(Cmd, cmdp + 1));
                        errors = 1;
                        break;
                }
                cmdp += 2;
                break;
            case 'b':
                switch (param_getchar(Cmd, cmdp + 1)) {
                    case '0':
                        config.forcebcc = 0;
                        break;
                    case '1':
                        config.forcebcc = 1;
                        break;
                    case '2':
                        config.forcebcc = 2;
                        break;
                    default:
                        PrintAndLogEx(WARNING, "Unknown value '%c'", param_getchar(Cmd, cmdp + 1));
                        errors = 1;
                        break;
                }
                cmdp += 2;
                break;
            case '2':
                switch (param_getchar(Cmd, cmdp + 1)) {
                    case '0':
                        config.forcecl2 = 0;
                        break;
                    case '1':
                        config.forcecl2 = 1;
                        break;
                    case '2':
                        config.forcecl2 = 2;
                        break;
                    default:
                        PrintAndLogEx(WARNING, "Unknown value '%c'", param_getchar(Cmd, cmdp + 1));
                        errors = 1;
                        break;
                }
                cmdp += 2;
                break;
            case '3':
                switch (param_getchar(Cmd, cmdp + 1)) {
                    case '0':
                        config.forcecl3 = 0;
                        break;
                    case '1':
                        config.forcecl3 = 1;
                        break;
                    case '2':
                        config.forcecl3 = 2;
                        break;
                    default:
                        PrintAndLogEx(WARNING, "Unknown value '%c'", param_getchar(Cmd, cmdp + 1));
                        errors = 1;
                        break;
                }
                cmdp += 2;
                break;
            case 'r':
                switch (param_getchar(Cmd, cmdp + 1)) {
                    case '0':
                        config.forcerats = 0;
                        break;
                    case '1':
                        config.forcerats = 1;
                        break;
                    case '2':
                        config.forcerats = 2;
                        break;
                    default:
                        PrintAndLogEx(WARNING, "Unknown value '%c'", param_getchar(Cmd, cmdp + 1));
                        errors = 1;
                        break;
                }
                cmdp += 2;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter '%c'", param_getchar(Cmd, cmdp));
                errors = 1;
                break;
        }
    }

    // validations
    if (errors) return usage_hf_14a_config();

    return hf14a_setconfig(&config);
}

int Hf14443_4aGetCardData(iso14a_card_select_t *card) {
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT, 0, 0, NULL, 0);

    PacketResponseNG resp;
    WaitForResponse(CMD_ACK, &resp);

    memcpy(card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

    uint64_t select_status = resp.oldarg[0]; // 0: couldn't read, 1: OK, with ATS, 2: OK, no ATS, 3: proprietary Anticollision

    if (select_status == 0) {
        PrintAndLogEx(ERR, "E->iso14443a card select failed");
        return 1;
    }

    if (select_status == 2) {
        PrintAndLogEx(ERR, "E->Card doesn't support iso14443-4 mode");
        return 1;
    }

    if (select_status == 3) {
        PrintAndLogEx(INFO, "E->Card doesn't support standard iso14443-3 anticollision");
        PrintAndLogEx(SUCCESS, "\tATQA : %02x %02x", card->atqa[1], card->atqa[0]);
        return 1;
    }

    PrintAndLogEx(SUCCESS, " UID: " _GREEN_("%s"), sprint_hex(card->uid, card->uidlen));
    PrintAndLogEx(SUCCESS, "ATQA: %02x %02x", card->atqa[1], card->atqa[0]);
    PrintAndLogEx(SUCCESS, " SAK: %02x [%" PRIu64 "]", card->sak, resp.oldarg[0]);
    if (card->ats_len < 3) { // a valid ATS consists of at least the length byte (TL) and 2 CRC bytes
        PrintAndLogEx(INFO, "E-> Error ATS length(%d) : %s", card->ats_len, sprint_hex(card->ats, card->ats_len));
        return 1;
    }

    PrintAndLogEx(SUCCESS, " ATS: %s", sprint_hex(card->ats, card->ats_len));
    return 0;
}

static int CmdHF14AReader(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a reader",
                  "Reader for ISO 14443A based tags",
                  "hf 14a reader -@ <- Continuous mode");

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("k", "keep", "keep the field active after command executed"),
        arg_lit0("s", "silent", "silent (no messages)"),
        arg_lit0(NULL, "drop", "just drop the signal field"),
        arg_lit0(NULL, "skip", "ISO14443-3 select only (skip RATS)"),
        arg_lit0("@", NULL, "optional - continuous reader mode"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    bool disconnectAfter = true;
    if (arg_get_lit(ctx, 1)) {
        disconnectAfter = false;
    }

    bool silent = arg_get_lit(ctx, 2);

    uint32_t cm = ISO14A_CONNECT;
    if (arg_get_lit(ctx, 3)) {
        cm &= ~ISO14A_CONNECT;
    }

    if (arg_get_lit(ctx, 4)) {
        cm |= ISO14A_NO_RATS;
    }

    bool continuous = arg_get_lit(ctx, 5);

    CLIParserFree(ctx);

    int res = PM3_SUCCESS;

    if (!disconnectAfter)
        cm |= ISO14A_NO_DISCONNECT;
    if (continuous) {
        PrintAndLogEx(INFO, "Press " _GREEN_("Enter") " to exit");
    }
    do {
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_ISO14443A_READER, cm, 0, 0, NULL, 0);

        if (ISO14A_CONNECT & cm) {
            PacketResponseNG resp;
            if (!WaitForResponseTimeout(CMD_ACK, &resp, 2500)) {
                if (!silent) PrintAndLogEx(WARNING, "iso14443a card select failed");
                DropField();
                res = PM3_ESOFT;
                goto plot;
            }

            iso14a_card_select_t card;
            memcpy(&card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

            /*
                0: couldn't read
                1: OK, with ATS
                2: OK, no ATS
                3: proprietary Anticollision
            */
            uint64_t select_status = resp.oldarg[0];

            if (select_status == 0) {
                if (!silent) PrintAndLogEx(WARNING, "iso14443a card select failed");
                DropField();
                res = PM3_ESOFT;
                goto plot;
            }

            if (select_status == 3) {
                if (!(silent && continuous)) {
                    PrintAndLogEx(INFO, "Card doesn't support standard iso14443-3 anticollision");
                    PrintAndLogEx(SUCCESS, "ATQA: %02x %02x", card.atqa[1], card.atqa[0]);
                }
                DropField();
                res = PM3_ESOFT;
                goto plot;
            }
            PrintAndLogEx(SUCCESS, " UID: " _GREEN_("%s"), sprint_hex(card.uid, card.uidlen));
            if (!(silent && continuous)) {
                PrintAndLogEx(SUCCESS, "ATQA: " _GREEN_("%02x %02x"), card.atqa[1], card.atqa[0]);
                PrintAndLogEx(SUCCESS, " SAK: " _GREEN_("%02x [%" PRIu64 "]"), card.sak, resp.oldarg[0]);

                if (card.ats_len >= 3) { // a valid ATS consists of at least the length byte (TL) and 2 CRC bytes
                    PrintAndLogEx(SUCCESS, " ATS: " _GREEN_("%s"), sprint_hex(card.ats, card.ats_len));
                }
            }
            if (!disconnectAfter) {
                if (!silent) PrintAndLogEx(SUCCESS, "Card is selected. You can now start sending commands");
            }
        }
plot:
        if (continuous) {
            res = handle_hf_plot();
            if (res != PM3_SUCCESS) {
                break;
            }
        }

        if (kbd_enter_pressed()) {
            break;
        }

    } while (continuous);

    if (disconnectAfter) {
        if (silent == false) {
            PrintAndLogEx(INFO, "field dropped.");
        }
    }

    if (continuous)
        return PM3_SUCCESS;
    else
        return res;
}

static int CmdHF14AInfo(const char *Cmd) {
    bool verbose = true;
    bool do_nack_test = false;
    bool do_aid_search = false;

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a info",
                  "This command makes more extensive tests against a ISO14443a tag in order to collect information",
                  "hf 14a info -nsv -> shows full information about the card\n");

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("v",  "verbose",   "adds some information to results"),
        arg_lit0("n",  "nacktest",   "test for nack bug"),
        arg_lit0("s",  "aidsearch", "checks if AIDs from aidlist.json is present on the card and prints information about found AIDs"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    verbose = arg_get_lit(ctx, 1);
    do_nack_test = arg_get_lit(ctx, 2);
    do_aid_search = arg_get_lit(ctx, 3);

    CLIParserFree(ctx);

    infoHF14A(verbose, do_nack_test, do_aid_search);
    return PM3_SUCCESS;
}

// Collect ISO14443 Type A UIDs
static int CmdHF14ACUIDs(const char *Cmd) {
    // requested number of UIDs
    int n = atoi(Cmd);
    // collect at least 1 (e.g. if no parameter was given)
    n = n > 0 ? n : 1;

    uint64_t t1 =  msclock();
    PrintAndLogEx(SUCCESS, "collecting %d UIDs", n);

    // repeat n times
    for (int i = 0; i < n; i++) {

        if (kbd_enter_pressed()) {
            PrintAndLogEx(WARNING, "aborted via keyboard!\n");
            break;
        }

        // execute anticollision procedure
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_RATS, 0, 0, NULL, 0);

        PacketResponseNG resp;
        WaitForResponse(CMD_ACK, &resp);

        iso14a_card_select_t *card = (iso14a_card_select_t *) resp.data.asBytes;

        // check if command failed
        if (resp.oldarg[0] == 0) {
            PrintAndLogEx(WARNING, "card select failed.");
        } else {
            char uid_string[20];
            for (uint16_t m = 0; m < card->uidlen; m++) {
                sprintf(&uid_string[2 * m], "%02X", card->uid[m]);
            }
            PrintAndLogEx(SUCCESS, "%s", uid_string);
        }
    }
    PrintAndLogEx(SUCCESS, "end: %" PRIu64 " seconds", (msclock() - t1) / 1000);
    return 1;
}
// ## simulate iso14443a tag
int CmdHF14ASim(const char *Cmd) {

    int uidlen = 0;
    uint8_t flags = 0, tagtype = 1, cmdp = 0;
    uint8_t uid[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool useUIDfromEML = true;
    bool setEmulatorMem = false;
    bool verbose = false;
    bool errors = false;
    sector_t *k_sector = NULL;
    uint8_t k_sectorsCount = 40;
    uint8_t exitAfterNReads = 0;

    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (tolower(param_getchar(Cmd, cmdp))) {
            case 'h':
                return usage_hf_14a_sim();
            case 't':
                // Retrieve the tag type
                tagtype = param_get8ex(Cmd, cmdp + 1, 0, 10);
                if (tagtype == 0)
                    errors = true;
                cmdp += 2;
                break;
            case 'u':
                // Retrieve the full 4,7,10 byte long uid
                param_gethex_ex(Cmd, cmdp + 1, uid, &uidlen);
                uidlen >>= 1;
                switch (uidlen) {
                    case 10:
                        flags |= FLAG_10B_UID_IN_DATA;
                        break;
                    case 7:
                        flags |= FLAG_7B_UID_IN_DATA;
                        break;
                    case 4:
                        flags |= FLAG_4B_UID_IN_DATA;
                        break;
                    default:
                        errors = true;
                        break;
                }
                if (!errors) {
                    PrintAndLogEx(SUCCESS, "Emulating " _YELLOW_("ISO/IEC 14443 type A tag")" with " _GREEN_("%d byte UID (%s)"), uidlen, sprint_hex(uid, uidlen));
                    useUIDfromEML = false;
                }
                cmdp += 2;
                break;
            case 'n':
                exitAfterNReads = param_get8(Cmd, cmdp + 1);
                cmdp += 2;
                break;
            case 'v':
                verbose = true;
                cmdp++;
                break;
            case 'x':
                flags |= FLAG_NR_AR_ATTACK;
                cmdp++;
                break;
            case 'e':
                setEmulatorMem = true;
                cmdp++;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter " _RED_("'%c'"), param_getchar(Cmd, cmdp));
                errors = true;
                break;
        }
    }

    //Validations
    if (errors || cmdp == 0) return usage_hf_14a_sim();

    if (useUIDfromEML)
        flags |= FLAG_UID_IN_EMUL;

    struct {
        uint8_t tagtype;
        uint8_t flags;
        uint8_t uid[10];
        uint8_t exitAfter;
    } PACKED payload;

    payload.tagtype = tagtype;
    payload.flags = flags;
    payload.exitAfter = exitAfterNReads;
    memcpy(payload.uid, uid, uidlen);

    clearCommandBuffer();
    SendCommandNG(CMD_HF_ISO14443A_SIMULATE, (uint8_t *)&payload, sizeof(payload));
    PacketResponseNG resp;

    PrintAndLogEx(INFO, "Press pm3-button to abort simulation");
    bool keypress = kbd_enter_pressed();
    while (!keypress) {

        if (WaitForResponseTimeout(CMD_HF_MIFARE_SIMULATE, &resp, 1500) == 0) continue;
        if (resp.status != PM3_SUCCESS) break;

        if ((flags & FLAG_NR_AR_ATTACK) != FLAG_NR_AR_ATTACK) break;

        nonces_t *data = (nonces_t *)resp.data.asBytes;
        readerAttack(k_sector, k_sectorsCount, data[0], setEmulatorMem, verbose);

        keypress = kbd_enter_pressed();
    }

    if (keypress && (flags & FLAG_NR_AR_ATTACK) == FLAG_NR_AR_ATTACK) {
        // inform device to break the sim loop since client has exited
        SendCommandNG(CMD_BREAK_LOOP, NULL, 0);
    }

    if (resp.status == PM3_EOPABORTED && ((flags & FLAG_NR_AR_ATTACK) == FLAG_NR_AR_ATTACK))
        showSectorTable(k_sector, k_sectorsCount);

    PrintAndLogEx(INFO, "Done");
    return PM3_SUCCESS;
}

int CmdHF14ASniff(const char *Cmd) {
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a sniff",
                  "Collect data from the field and save into command buffer.\n"
                  "Buffer accessible from command 'hf 14a list'",
                  " hf 14a sniff -c -r");

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("c", "card", "triggered by first data from card"),
        arg_lit0("r", "reader", "triggered by first 7-bit request from reader (REQ,WUP,...)"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    uint8_t param = 0;

    if (arg_get_lit(ctx, 1)) {
        param |= 0x01;
    }

    if (arg_get_lit(ctx, 2)) {
        param |= 0x02;
    }

    CLIParserFree(ctx);

    clearCommandBuffer();
    SendCommandNG(CMD_HF_ISO14443A_SNIFF, (uint8_t *)&param, sizeof(uint8_t));
    return PM3_SUCCESS;
}

int ExchangeRAW14a(uint8_t *datain, int datainlen, bool activateField, bool leaveSignalON, uint8_t *dataout, int maxdataoutlen, int *dataoutlen, bool silentMode) {
    static uint8_t responseNum = 0;
    uint16_t cmdc = 0;
    *dataoutlen = 0;

    if (activateField) {
        PacketResponseNG resp;
        responseNum = 0;

        // Anticollision + SELECT card
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT, 0, 0, NULL, 0);
        if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
            if (!silentMode) PrintAndLogEx(ERR, "Proxmark3 connection timeout.");
            return 1;
        }

        // check result
        if (resp.oldarg[0] == 0) {
            if (!silentMode) PrintAndLogEx(ERR, "No card in field.");
            return 1;
        }

        if (resp.oldarg[0] != 1 && resp.oldarg[0] != 2) {
            if (!silentMode) PrintAndLogEx(ERR, "Card not in iso14443-4. res=%" PRId64 ".", resp.oldarg[0]);
            return 1;
        }

        if (resp.oldarg[0] == 2) { // 0: couldn't read, 1: OK, with ATS, 2: OK, no ATS, 3: proprietary Anticollision
            // get ATS
            uint8_t rats[] = { 0xE0, 0x80 }; // FSDI=8 (FSD=256), CID=0
            SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_APPEND_CRC | ISO14A_NO_DISCONNECT, 2, 0, rats, sizeof(rats));
            if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
                if (!silentMode) PrintAndLogEx(ERR, "Proxmark3 connection timeout.");
                return 1;
            }

            if (resp.oldarg[0] == 0) { // ats_len
                if (!silentMode) PrintAndLogEx(ERR, "Can't get ATS.");
                return 1;
            }
        }
    }

    if (leaveSignalON)
        cmdc |= ISO14A_NO_DISCONNECT;

    uint8_t data[PM3_CMD_DATA_SIZE] = { 0x0a | responseNum, 0x00};
    responseNum ^= 1;
    memcpy(&data[2], datain, datainlen & 0xFFFF);
    SendCommandOLD(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_APPEND_CRC | cmdc, (datainlen & 0xFFFF) + 2, 0, data, (datainlen & 0xFFFF) + 2);

    uint8_t *recv;
    PacketResponseNG resp;

    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        recv = resp.data.asBytes;
        int iLen = resp.oldarg[0];

        if (!iLen) {
            if (!silentMode) PrintAndLogEx(ERR, "No card response.");
            return 1;
        }

        *dataoutlen = iLen - 2;
        if (*dataoutlen < 0)
            *dataoutlen = 0;

        if (maxdataoutlen && *dataoutlen > maxdataoutlen) {
            if (!silentMode) PrintAndLogEx(ERR, "Buffer too small(%d). Needs %d bytes", *dataoutlen, maxdataoutlen);
            return 2;
        }

        if (recv[0] != data[0]) {
            if (!silentMode) PrintAndLogEx(ERR, "iso14443-4 framing error. Card send %2x must be %2x", dataout[0], data[0]);
            return 2;
        }

        memcpy(dataout, &recv[2], *dataoutlen);

        // CRC Check
        if (iLen == -1) {
            if (!silentMode) PrintAndLogEx(ERR, "ISO 14443A CRC error.");
            return 3;
        }

    } else {
        if (!silentMode) PrintAndLogEx(ERR, "Reply timeout.");
        return 4;
    }

    return 0;
}

static int SelectCard14443_4(bool disconnect, iso14a_card_select_t *card) {
    PacketResponseNG resp;

    frameLength = 0;

    if (card)
        memset(card, 0, sizeof(iso14a_card_select_t));

    DropField();

    // Anticollision + SELECT card
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT, 0, 0, NULL, 0);
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        PrintAndLogEx(ERR, "Proxmark3 connection timeout.");
        return 1;
    }

    // check result
    if (resp.oldarg[0] == 0) {
        PrintAndLogEx(ERR, "No card in field.");
        return 1;
    }

    if (resp.oldarg[0] != 1 && resp.oldarg[0] != 2) {
        PrintAndLogEx(ERR, "Card not in iso14443-4. res=%" PRId64 ".", resp.oldarg[0]);
        return 1;
    }

    if (resp.oldarg[0] == 2) { // 0: couldn't read, 1: OK, with ATS, 2: OK, no ATS, 3: proprietary Anticollision
        // get ATS
        uint8_t rats[] = { 0xE0, 0x80 }; // FSDI=8 (FSD=256), CID=0
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_APPEND_CRC | ISO14A_NO_DISCONNECT, sizeof(rats), 0, rats, sizeof(rats));
        if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
            PrintAndLogEx(ERR, "Proxmark3 connection timeout.");
            return 1;
        }

        if (resp.oldarg[0] == 0) { // ats_len
            PrintAndLogEx(ERR, "Can't get ATS.");
            return 1;
        }

        // get frame length from ATS in data field
        if (resp.oldarg[0] > 1) {
            uint8_t fsci = resp.data.asBytes[1] & 0x0f;
            if (fsci < ARRAYLEN(atsFSC))
                frameLength = atsFSC[fsci];
        }
    } else {
        // get frame length from ATS in card data structure
        iso14a_card_select_t *vcard = (iso14a_card_select_t *) resp.data.asBytes;
        if (vcard->ats_len > 1) {
            uint8_t fsci = vcard->ats[1] & 0x0f;
            if (fsci < ARRAYLEN(atsFSC))
                frameLength = atsFSC[fsci];
        }

        if (card)
            memcpy(card, vcard, sizeof(iso14a_card_select_t));
    }

    if (disconnect)
        DropField();

    return 0;
}

static int CmdExchangeAPDU(bool chainingin, uint8_t *datain, int datainlen, bool activateField, uint8_t *dataout, int maxdataoutlen, int *dataoutlen, bool *chainingout) {
    *chainingout = false;

    if (activateField) {
        // select with no disconnect and set frameLength
        int selres = SelectCard14443_4(false, NULL);
        if (selres)
            return selres;
    }

    uint16_t cmdc = 0;
    if (chainingin)
        cmdc = ISO14A_SEND_CHAINING;

    // "Command APDU" length should be 5+255+1, but javacard's APDU buffer might be smaller - 133 bytes
    // https://stackoverflow.com/questions/32994936/safe-max-java-card-apdu-data-command-and-respond-size
    // here length PM3_CMD_DATA_SIZE=512
    // timeout must be authomatically set by "get ATS"
    if (datain)
        SendCommandOLD(CMD_HF_ISO14443A_READER, ISO14A_APDU | ISO14A_NO_DISCONNECT | cmdc, (datainlen & 0xFFFF), 0, datain, datainlen & 0xFFFF);
    else
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_APDU | ISO14A_NO_DISCONNECT | cmdc, 0, 0, NULL, 0);

    PacketResponseNG resp;

    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        uint8_t *recv = resp.data.asBytes;
        int iLen = resp.oldarg[0];
        uint8_t res = resp.oldarg[1];

        int dlen = iLen - 2;
        if (dlen < 0)
            dlen = 0;
        *dataoutlen += dlen;

        if (maxdataoutlen && *dataoutlen > maxdataoutlen) {
            PrintAndLogEx(ERR, "APDU: Buffer too small(%d). Needs %d bytes", *dataoutlen, maxdataoutlen);
            return 2;
        }

        // I-block ACK
        if ((res & 0xf2) == 0xa2) {
            *dataoutlen = 0;
            *chainingout = true;
            return 0;
        }

        if (!iLen) {
            PrintAndLogEx(ERR, "APDU: No APDU response.");
            return 1;
        }

        // check apdu length
        if (iLen < 2 && iLen >= 0) {
            PrintAndLogEx(ERR, "APDU: Small APDU response. Len=%d", iLen);
            return 2;
        }

        // check block TODO
        if (iLen == -2) {
            PrintAndLogEx(ERR, "APDU: Block type mismatch.");
            return 2;
        }

        memcpy(dataout, recv, dlen);

        // chaining
        if ((res & 0x10) != 0) {
            *chainingout = true;
        }

        // CRC Check
        if (iLen == -1) {
            PrintAndLogEx(ERR, "APDU: ISO 14443A CRC error.");
            return 3;
        }
    } else {
        PrintAndLogEx(ERR, "APDU: Reply timeout.");
        return 4;
    }

    return PM3_SUCCESS;
}

int ExchangeAPDU14a(uint8_t *datain, int datainlen, bool activateField, bool leaveSignalON, uint8_t *dataout, int maxdataoutlen, int *dataoutlen) {
    *dataoutlen = 0;
    bool chaining = false;
    int res;

    // 3 byte here - 1b framing header, 2b crc16
    if (APDUInFramingEnable &&
            ((frameLength && (datainlen > frameLength - 3)) || (datainlen > PM3_CMD_DATA_SIZE - 3))) {
        int clen = 0;

        bool vActivateField = activateField;

        do {
            int vlen = MIN(frameLength - 3, datainlen - clen);
            bool chainBlockNotLast = ((clen + vlen) < datainlen);

            *dataoutlen = 0;
            res = CmdExchangeAPDU(chainBlockNotLast, &datain[clen], vlen, vActivateField, dataout, maxdataoutlen, dataoutlen, &chaining);
            if (res) {
                if (!leaveSignalON)
                    DropField();

                return 200;
            }

            // check R-block ACK
//TODO check this one...
            if ((*dataoutlen == 0) && (*dataoutlen != 0 || chaining != chainBlockNotLast)) { // *dataoutlen!=0. 'A && (!A || B)' is equivalent to 'A && B'
                if (!leaveSignalON)
                    DropField();

                return 201;
            }

            clen += vlen;
            vActivateField = false;
            if (*dataoutlen) {
                if (clen != datainlen)
                    PrintAndLogEx(ERR, "APDU: I-block/R-block sequence error. Data len=%d, Sent=%d, Last packet len=%d", datainlen, clen, *dataoutlen);
                break;
            }
        } while (clen < datainlen);
    } else {
        res = CmdExchangeAPDU(false, datain, datainlen, activateField, dataout, maxdataoutlen, dataoutlen, &chaining);
        if (res) {
            if (!leaveSignalON)
                DropField();

            return res;
        }
    }

    while (chaining) {
        // I-block with chaining
        res = CmdExchangeAPDU(false, NULL, 0, false, &dataout[*dataoutlen], maxdataoutlen, dataoutlen, &chaining);

        if (res) {
            if (!leaveSignalON)
                DropField();

            return 100;
        }
    }

    if (!leaveSignalON)
        DropField();

    return 0;
}

// ISO14443-4. 7. Half-duplex block transmission protocol
static int CmdHF14AAPDU(const char *Cmd) {
    uint8_t data[PM3_CMD_DATA_SIZE];
    int datalen = 0;
    uint8_t header[PM3_CMD_DATA_SIZE];
    int headerlen = 0;
    bool activateField = false;
    bool leaveSignalON = false;
    bool decodeTLV = false;
    bool decodeAPDU = false;
    bool makeAPDU = false;
    bool extendedAPDU = false;
    int le = 0;

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a apdu",
                  "Sends an ISO 7816-4 APDU via ISO 14443-4 block transmission protocol (T=CL). works with all apdu types from ISO 7816-4:2013",
                  "hf 14a apdu -st 00A404000E325041592E5359532E444446303100\n"
                  "hf 14a apdu -sd 00A404000E325041592E5359532E444446303100        -> decode apdu\n"
                  "hf 14a apdu -sm 00A40400 325041592E5359532E4444463031 -l 256    -> encode standard apdu\n"
                  "hf 14a apdu -sm 00A40400 325041592E5359532E4444463031 -el 65536 -> encode extended apdu\n");

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("s",  "select",   "activate field and select card"),
        arg_lit0("k",  "keep",     "keep signal field ON after receive"),
        arg_lit0("t",  "tlv",      "executes TLV decoder if it possible"),
        arg_lit0("d",  "decapdu",  "decode apdu request if it possible"),
        arg_str0("m",  "make",     "<head (CLA INS P1 P2) hex>", "make apdu with head from this field and data from data field. Must be 4 bytes length: <CLA INS P1 P2>"),
        arg_lit0("e",  "extended", "make extended length apdu if `m` parameter included"),
        arg_int0("l",  "le",       "<Le (int)>", "Le apdu parameter if `m` parameter included"),
        arg_strx1(NULL, NULL,       "<APDU (hex) | data (hex)>", "data if `m` parameter included"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    activateField = arg_get_lit(ctx, 1);
    leaveSignalON = arg_get_lit(ctx, 2);
    decodeTLV = arg_get_lit(ctx, 3);
    decodeAPDU = arg_get_lit(ctx, 4);

    CLIGetHexWithReturn(ctx, 5, header, &headerlen);
    makeAPDU = headerlen > 0;
    if (makeAPDU && headerlen != 4) {
        PrintAndLogEx(ERR, "header length must be 4 bytes instead of %d", headerlen);
        CLIParserFree(ctx);
        return PM3_EINVARG;
    }
    extendedAPDU = arg_get_lit(ctx, 6);
    le = arg_get_int_def(ctx, 7, 0);

    if (makeAPDU) {
        uint8_t apdudata[PM3_CMD_DATA_SIZE] = {0};
        int apdudatalen = 0;

        CLIGetHexBLessWithReturn(ctx, 8, apdudata, &apdudatalen, 1 + 2);

        APDUStruct apdu;
        apdu.cla = header[0];
        apdu.ins = header[1];
        apdu.p1 = header[2];
        apdu.p2 = header[3];

        apdu.lc = apdudatalen;
        apdu.data = apdudata;

        apdu.extended_apdu = extendedAPDU;
        apdu.le = le;

        if (APDUEncode(&apdu, data, &datalen)) {
            PrintAndLogEx(ERR, "can't make apdu with provided parameters.");
            CLIParserFree(ctx);
            return PM3_EINVARG;
        }

    } else {
        if (extendedAPDU) {
            PrintAndLogEx(ERR, "make mode not set but here `e` option.");
            CLIParserFree(ctx);
            return PM3_EINVARG;
        }
        if (le > 0) {
            PrintAndLogEx(ERR, "make mode not set but here `l` option.");
            CLIParserFree(ctx);
            return PM3_EINVARG;
        }

        // len = data + PCB(1b) + CRC(2b)
        CLIGetHexBLessWithReturn(ctx, 8, data, &datalen, 1 + 2);
    }
    CLIParserFree(ctx);

    PrintAndLogEx(SUCCESS, "( " _YELLOW_("%s%s%s")" )",
                  activateField ? "select" : "",
                  leaveSignalON ? ", keep" : "",
                  decodeTLV ? ", TLV" : ""
                 );
    PrintAndLogEx(SUCCESS, ">>> %s", sprint_hex_inrow(data, datalen));

    if (decodeAPDU) {
        APDUStruct apdu;

        if (APDUDecode(data, datalen, &apdu) == 0)
            APDUPrint(apdu);
        else
            PrintAndLogEx(WARNING, "can't decode APDU.");
    }

    int res = ExchangeAPDU14a(data, datalen, activateField, leaveSignalON, data, PM3_CMD_DATA_SIZE, &datalen);

    if (res)
        return res;

    PrintAndLogEx(SUCCESS, "<<< %s | %s", sprint_hex_inrow(data, datalen), sprint_ascii(data, datalen));
    PrintAndLogEx(SUCCESS, "<<< status: %02x %02x - %s", data[datalen - 2], data[datalen - 1], GetAPDUCodeDescription(data[datalen - 2], data[datalen - 1]));

    // TLV decoder
    if (decodeTLV && datalen > 4) {
        TLVPrintFromBuffer(data, datalen - 2);
    }

    return PM3_SUCCESS;
}

static int CmdHF14ACmdRaw(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a raw",
                  "Sends an raw bytes over ISO14443a. With option to use TOPAZ 14a mode.",
                  "hf 14a raw -sc 3000     -> select, crc, where 3000 == 'read block 00'\n"
                  "hf 14a raw -ak -b 7 40  -> send 7 bit byte 0x40\n"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("a",  NULL, "active signal field ON without select"),
        arg_int0("b",  NULL, "<dec>", "number of bits to send. Useful for send partial byte"),
        arg_lit0("c",  NULL, "calculate and append CRC"),
        arg_lit0("k",  NULL, "keep signal field ON after receive"),
        arg_lit0("3",  NULL, "ISO14443-3 select only (skip RATS)"),
        arg_lit0("r",  NULL, "do not read response"),
        arg_lit0("s",  NULL, "active signal field ON with select"),
        arg_int0("t",  "timeout", "<ms>", "timeout in milliseconds"),
        arg_lit0(NULL, "topaz", "use Topaz protocol to send command"),
        arg_strx1(NULL, NULL, "<hex>", "raw bytes to send"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);


    bool active = arg_get_lit(ctx, 1);
    uint16_t numbits = (uint16_t)arg_get_int_def(ctx, 2, 0);
    bool crc = arg_get_lit(ctx, 3);
    bool keep_field_on = arg_get_lit(ctx, 4);
    bool no_rats =  arg_get_lit(ctx, 5);
    bool reply = (arg_get_lit(ctx, 6) == false);
    bool active_select = arg_get_lit(ctx, 7);
    uint32_t timeout = (uint32_t)arg_get_int_def(ctx, 8, 0);
    bool topazmode = arg_get_lit(ctx, 9);

    int datalen = 0;
    uint8_t data[PM3_CMD_DATA_SIZE];
    CLIGetHexWithReturn(ctx, 10, data, &datalen);
    CLIParserFree(ctx);

    bool bTimeout = (timeout) ? true : false;

    // ensure we can add 2byte crc to input data
    if (datalen >= sizeof(data) + 2) {
        if (crc) {
            PrintAndLogEx(FAILED, "Buffer is full, we can't add CRC to your data");
            return PM3_EINVARG;
        }
    }

    if (crc && datalen > 0 && datalen < sizeof(data) - 2) {
        uint8_t first, second;
        if (topazmode) {
            compute_crc(CRC_14443_B, data, datalen, &first, &second);
        } else {
            compute_crc(CRC_14443_A, data, datalen, &first, &second);
        }
        data[datalen++] = first;
        data[datalen++] = second;
    }

    uint16_t flags = 0;
    if (active || active_select) {
        flags |= ISO14A_CONNECT;
        if (active)
            flags |= ISO14A_NO_SELECT;
    }

    uint32_t argtimeout = 0;
    if (bTimeout) {
#define MAX_TIMEOUT 40542464 // = (2^32-1) * (8*16) / 13560000Hz * 1000ms/s
        flags |= ISO14A_SET_TIMEOUT;
        if (timeout > MAX_TIMEOUT) {
            timeout = MAX_TIMEOUT;
            PrintAndLogEx(INFO, "Set timeout to 40542 seconds (11.26 hours). The max we can wait for response");
        }
        argtimeout = 13560000 / 1000 / (8 * 16) * timeout; // timeout in ETUs (time to transfer 1 bit, approx. 9.4 us)
    }

    if (keep_field_on) {
        flags |= ISO14A_NO_DISCONNECT;
    }

    if (datalen > 0) {
        flags |= ISO14A_RAW;
    }

    if (topazmode) {
        flags |= ISO14A_TOPAZMODE;
    }
    if (no_rats) {
        flags |= ISO14A_NO_RATS;
    }

    // Max buffer is PM3_CMD_DATA_SIZE
    datalen = (datalen > PM3_CMD_DATA_SIZE) ? PM3_CMD_DATA_SIZE : datalen;

    clearCommandBuffer();
    SendCommandOLD(CMD_HF_ISO14443A_READER, flags, (datalen & 0xFFFF) | ((uint32_t)(numbits << 16)), argtimeout, data, datalen & 0xFFFF);

    if (reply) {
        int res = 0;
        if (active_select)
            res = waitCmd(true, timeout);
        if (res == PM3_SUCCESS && datalen > 0)
            waitCmd(false, timeout);
    }
    return PM3_SUCCESS;
}

static int waitCmd(bool i_select, uint32_t timeout) {
    PacketResponseNG resp;

    if (WaitForResponseTimeout(CMD_ACK, &resp, timeout + 1500)) {
        uint16_t len = (resp.oldarg[0] & 0xFFFF);
        if (i_select) {
            len = (resp.oldarg[1] & 0xFFFF);
            if (len) {
                PrintAndLogEx(SUCCESS, "Card selected. UID[%u]:", len);
            } else {
                PrintAndLogEx(WARNING, "Can't select card.");
            }
        } else {
            PrintAndLogEx(SUCCESS, "received " _YELLOW_("%u") " bytes", len);
        }

        if (!len)
            return PM3_ESOFT;

        uint8_t *data = resp.data.asBytes;

        if (i_select == false && len >= 3) {
            bool crc = check_crc(CRC_14443_A, data, len);

            char s[16];
            sprintf(s,
                    (crc) ? _GREEN_("%02X %02X") : _RED_("%02X %02X"),
                    data[len - 2],
                    data[len - 1]
                   );

            PrintAndLogEx(SUCCESS, "%s[ %s ]",  sprint_hex(data, len - 2), s);
        } else {
            PrintAndLogEx(SUCCESS, "%s", sprint_hex(data, len));
        }

    } else {
        PrintAndLogEx(WARNING, "timeout while waiting for reply.");
        return PM3_ETIMEOUT;
    }
    return PM3_SUCCESS;
}

static int CmdHF14AAntiFuzz(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a antifuzz",
                  "Tries to fuzz the ISO14443a anticollision phase",
                  "hf 14a antifuzz -4\n");

    void *argtable[] = {
        arg_param_begin,
        arg_lit0("4",   NULL,  "4 byte uid"),
        arg_lit0("7",   NULL,  "7 byte uid"),
        arg_lit0(NULL,  "10",  "10 byte uid"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    struct {
        uint8_t flag;
    } PACKED param;
    param.flag = FLAG_4B_UID_IN_DATA;

    if (arg_get_lit(ctx, 2))
        param.flag = FLAG_7B_UID_IN_DATA;
    if (arg_get_lit(ctx, 3))
        param.flag = FLAG_10B_UID_IN_DATA;

    CLIParserFree(ctx);
    clearCommandBuffer();
    SendCommandNG(CMD_HF_ISO14443A_ANTIFUZZ, (uint8_t *)&param, sizeof(param));
    return PM3_SUCCESS;
}

static int CmdHF14AChaining(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a chaining",
                  "Enable/Disable ISO14443a input chaining. Maximum input length goes from ATS.",
                  "hf 14a chaining disable -> disable chaining\n"
                  "hf 14a chaining         -> show chaining enable/disable state\n");

    void *argtable[] = {
        arg_param_begin,
        arg_str0(NULL, NULL,      "<enable/disable or 0/1>", NULL),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    struct arg_str *str = arg_get_str(ctx, 1);
    int len = arg_get_str_len(ctx, 1);

    if (len && (!strcmp(str->sval[0], "enable") || !strcmp(str->sval[0], "1")))
        APDUInFramingEnable = true;

    if (len && (!strcmp(str->sval[0], "disable") || !strcmp(str->sval[0], "0")))
        APDUInFramingEnable = false;

    CLIParserFree(ctx);

    PrintAndLogEx(INFO, "\nISO 14443-4 input chaining %s.\n", APDUInFramingEnable ? "enabled" : "disabled");

    return PM3_SUCCESS;
}

static void printTag(const char *tag) {
    PrintAndLogEx(SUCCESS, "   " _YELLOW_("%s"), tag);
}

typedef enum {
    MTNONE = 0,
    MTCLASSIC = 1,
    MTMINI = 2,
    MTDESFIRE = 4,
    MTPLUS = 8,
    MTULTRALIGHT = 16,
    MTOTHER = 32
} nxp_mifare_type_t;

// Based on NXP AN10833 Rev 3.6 and NXP AN10834 Rev 4.1
static int detect_nxp_card(uint8_t sak, uint16_t atqa, uint64_t select_status) {
    int type = MTNONE;

    PrintAndLogEx(SUCCESS, "Possible types:");

    if ((sak & 0x02) != 0x02) {
        if ((sak & 0x19) == 0x19) {
            printTag("MIFARE Classic 2K");
            type |= MTCLASSIC;
        } else if ((sak & 0x38) == 0x38) {
            printTag("SmartMX with MIFARE Classic 4K");
            type |= MTCLASSIC;
        } else if ((sak & 0x18) == 0x18) {
            if (select_status == 1) {
                if ((atqa & 0x0040) == 0x0040) {
                    printTag("MIFARE Plus EV1 4K CL2 in SL1");
                    printTag("MIFARE Plus S 4K CL2 in SL1");
                    printTag("MIFARE Plus X 4K CL2 in SL1");
                } else {
                    printTag("MIFARE Plus EV1 4K in SL1");
                    printTag("MIFARE Plus S 4K in SL1");
                    printTag("MIFARE Plus X 4K in SL1");
                }

                type |= MTPLUS;
            } else {
                if ((atqa & 0x0040) == 0x0040) {
                    printTag("MIFARE Classic 4K CL2");
                } else {
                    printTag("MIFARE Classic 4K");
                }

                type |= MTCLASSIC;
            }
        } else if ((sak & 0x09) == 0x09) {
            if ((atqa & 0x0040) == 0x0040) {
                printTag("MIFARE Mini 0.3K CL2");
            } else {
                printTag("MIFARE Mini 0.3K");
            }

            type |= MTMINI;
        } else if ((sak & 0x28) == 0x28) {
            printTag("SmartMX with MIFARE Classic 1K");
            type |= MTCLASSIC;
        } else if ((sak & 0x08) == 0x08) {
            if (select_status == 1) {
                if ((atqa & 0x0040) == 0x0040) {
                    printTag("MIFARE Plus EV1 2K CL2 in SL1");
                    printTag("MIFARE Plus S 2K CL2 in SL1");
                    printTag("MIFARE Plus X 2K CL2 in SL1");
                    printTag("MIFARE Plus SE 1K CL2");
                } else {
                    printTag("MIFARE Plus EV1 2K in SL1");
                    printTag("MIFARE Plus S 2K in SL1");
                    printTag("MIFARE Plus X 2K in SL1");
                    printTag("MIFARE Plus SE 1K");
                }

                type |= MTPLUS;
            } else {
                if ((atqa & 0x0040) == 0x0040) {
                    printTag("MIFARE Classic 1K CL2");
                } else {
                    printTag("MIFARE Classic 1K");
                }

                type |= MTCLASSIC;
            }
        } else if ((sak & 0x11) == 0x11) {
            printTag("MIFARE Plus 4K in SL2");
            type |= MTPLUS;
        } else if ((sak & 0x10) == 0x10) {
            printTag("MIFARE Plus 2K in SL2");
            type |= MTPLUS;
        } else if ((sak & 0x01) == 0x01) {
            printTag("TNP3xxx (TagNPlay, Activision Game Appliance)");
            type |= MTCLASSIC;
        } else if ((sak & 0x24) == 0x24) {
            printTag("MIFARE DESFire CL1");
            printTag("MIFARE DESFire EV1 CL1");
            type |= MTDESFIRE;
        } else if ((sak & 0x20) == 0x20) {
            if (select_status == 1) {
                if ((atqa & 0x0040) == 0x0040) {
                    if ((atqa & 0x0300) == 0x0300) {
                        printTag("MIFARE DESFire CL2");
                        printTag("MIFARE DESFire EV1 256B/2K/4K/8K CL2");
                        printTag("MIFARE DESFire EV2 2K/4K/8K/16K/32K");
                        printTag("MIFARE DESFire Light 640B");
                    } else {
                        printTag("MIFARE Plus EV1 2K/4K CL2 in SL3");
                        printTag("MIFARE Plus S 2K/4K CL2 in SL3");
                        printTag("MIFARE Plus X 2K/4K CL2 in SL3");
                        printTag("MIFARE Plus SE 1K CL2");
                        type |= MTPLUS;
                    }
                } else {
                    printTag("MIFARE Plus EV1 2K/4K in SL3");
                    printTag("MIFARE Plus S 2K/4K in SL3");
                    printTag("MIFARE Plus X 2K/4K in SL3");
                    printTag("MIFARE Plus SE 1K");
                    type |= MTPLUS;
                }

                printTag("NTAG 4xx");
                type |= MTDESFIRE;
            }
        } else if ((sak & 0x04) == 0x04) {
            printTag("Any MIFARE CL1");
            type |= MTDESFIRE;
        } else {
            printTag("MIFARE Ultralight");
            printTag("MIFARE Ultralight C");
            printTag("MIFARE Ultralight EV1");
            printTag("MIFARE Ultralight Nano");
            printTag("MIFARE Hospitality");
            printTag("NTAG 2xx");
            type |= MTULTRALIGHT;
        }
    }

    if (type == MTNONE) {
        PrintAndLogEx(WARNING, "   failed to fingerprint");
    }
    return type;
}

typedef struct {
    uint8_t uid0;
    uint8_t uid1;
    const char *desc;
} uid_label_name;

const uid_label_name uid_label_map[] = {
    // UID0, UID1, TEXT
    {0x02, 0x84, "M24SR64-Y"},
    {0x02, 0xA3, "25TA02KB-P"},
    {0x02, 0xC4, "25TA64K"},
    {0x02, 0xE3, "25TA02KB"},
    {0x02, 0xE4, "25TA512B"},
    {0x02, 0xF3, "25TA02KB-D"},
    {0x11, 0x22, "NTAG21x Modifiable"},
    {0x00, 0x00, "None"}
};

static void getTagLabel(uint8_t uid0, uint8_t uid1) {
    int i = 0;
    while (uid_label_map[i].uid0 != 0x00) {
        if ((uid_label_map[i].uid0 == uid0) && (uid_label_map[i].uid1 == uid1)) {
            PrintAndLogEx(SUCCESS, _YELLOW_("    %s"), uid_label_map[i].desc);
            return;
        }
        i += 1;
    }
}

int infoHF14A(bool verbose, bool do_nack_test, bool do_aid_search) {
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT, 0, 0, NULL, 0);
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 2500)) {
        if (verbose) PrintAndLogEx(WARNING, "iso14443a card select failed");
        DropField();
        return 0;
    }

    iso14a_card_select_t card;
    memcpy(&card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

    /*
        0: couldn't read
        1: OK, with ATS
        2: OK, no ATS
        3: proprietary Anticollision
    */
    uint64_t select_status = resp.oldarg[0];

    if (select_status == 0) {
        if (verbose) PrintAndLogEx(WARNING, "iso14443a card select failed");
        DropField();
        return select_status;
    }

    PrintAndLogEx(NORMAL, "");

    if (select_status == 3) {
        PrintAndLogEx(INFO, "Card doesn't support standard iso14443-3 anticollision");
        PrintAndLogEx(SUCCESS, "ATQA: %02x %02x", card.atqa[1], card.atqa[0]);
        DropField();
        return select_status;
    }

    if (verbose) {
        PrintAndLogEx(SUCCESS, "------ " _CYAN_("ISO14443-a Information") "------------------");
        PrintAndLogEx(SUCCESS, "-------------------------------------------------------------");
    }

    PrintAndLogEx(SUCCESS, " UID: " _GREEN_("%s"), sprint_hex(card.uid, card.uidlen));
    PrintAndLogEx(SUCCESS, "ATQA: " _GREEN_("%02x %02x"), card.atqa[1], card.atqa[0]);
    PrintAndLogEx(SUCCESS, " SAK: " _GREEN_("%02x [%" PRIu64 "]"), card.sak, resp.oldarg[0]);

    bool isMifareClassic = true;
    bool isMifareDESFire = false;
    bool isMifarePlus = false;
    bool isMifareUltralight = false;
    bool isST = false;
    int nxptype = MTNONE;

    if (card.uidlen <= 4) {
        nxptype = detect_nxp_card(card.sak, ((card.atqa[1] << 8) + card.atqa[0]), select_status);

        isMifareClassic = ((nxptype & MTCLASSIC) == MTCLASSIC);
        isMifareDESFire = ((nxptype & MTDESFIRE) == MTDESFIRE);
        isMifarePlus = ((nxptype & MTPLUS) == MTPLUS);
        isMifareUltralight = ((nxptype & MTULTRALIGHT) == MTULTRALIGHT);

        if ((nxptype & MTOTHER) == MTOTHER)
            isMifareClassic = true;

    } else {

        // Double & triple sized UID, can be mapped to a manufacturer.
        PrintAndLogEx(SUCCESS, "MANUFACTURER:    " _YELLOW_("%s"), getTagInfo(card.uid[0]));

        switch (card.uid[0]) {
            case 0x02: // ST
                isST = true;
                break;
            case 0x04: // NXP
                nxptype = detect_nxp_card(card.sak, ((card.atqa[1] << 8) + card.atqa[0]), select_status);

                isMifareClassic = ((nxptype & MTCLASSIC) == MTCLASSIC);
                isMifareDESFire = ((nxptype & MTDESFIRE) == MTDESFIRE);
                isMifarePlus = ((nxptype & MTPLUS) == MTPLUS);
                isMifareUltralight = ((nxptype & MTULTRALIGHT) == MTULTRALIGHT);

                if ((nxptype & MTOTHER) == MTOTHER)
                    isMifareClassic = true;

                break;
            case 0x05: // Infineon
                if ((card.uid[1] & 0xF0) == 0x10) {
                    printTag("my-d(tm) command set SLE 66R04/16/32P, SLE 66R04/16/32S");
                } else if ((card.uid[1] & 0xF0) == 0x20) {
                    printTag("my-d(tm) command set SLE 66R01/16/32P (Type 2 Tag)");
                } else if ((card.uid[1] & 0xF0) == 0x30) {
                    printTag("my-d(tm) move lean SLE 66R01P/66R01PN");
                } else if ((card.uid[1] & 0xF0) == 0x70) {
                    printTag("my-d(tm) move lean SLE 66R01L");
                }
                isMifareUltralight = true;
                isMifareClassic = false;

                if (card.sak == 0x88) {
                    printTag("Infineon MIFARE CLASSIC 1K");
                    isMifareUltralight = false;
                    isMifareClassic = true;
                }
                getTagLabel(card.uid[0], card.uid[1]);
                break;
            case 0x46:
                if (memcmp(card.uid, "FSTN10m", 7) == 0) {
                    isMifareClassic = false;
                    printTag("Waveshare NFC-Powered e-Paper 1.54\" (please disregard MANUFACTURER mapping above)");
                }
                break;
            case 0x57:
                if (memcmp(card.uid, "WSDZ10m", 7) == 0) {
                    isMifareClassic = false;
                    printTag("Waveshare NFC-Powered e-Paper (please disregard MANUFACTURER mapping above)");
                }
                break;
            default:
                getTagLabel(card.uid[0], card.uid[1]);
                switch (card.sak) {
                    case 0x00: {
                        isMifareClassic = false;

                        // ******** is card of the MFU type (UL/ULC/NTAG/ etc etc)
                        DropField();

                        uint32_t tagT = GetHF14AMfU_Type();
                        if (tagT != UL_ERROR) {
                            ul_print_type(tagT, 0);
                            isMifareUltralight = true;
                            printTag("MIFARE Ultralight/C/NTAG Compatible");
                        } else {
                            printTag("Possible AZTEK (iso14443a compliant)");
                        }

                        // reconnect for further tests
                        clearCommandBuffer();
                        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT, 0, 0, NULL, 0);
                        WaitForResponse(CMD_ACK, &resp);

                        memcpy(&card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

                        select_status = resp.oldarg[0]; // 0: couldn't read, 1: OK, with ATS, 2: OK, no ATS

                        if (select_status == 0) {
                            DropField();
                            return select_status;
                        }
                        break;
                    }
                    case 0x0A: {
                        printTag("FM11RF005SH (Shanghai Metro)");
                        break;
                    }
                    case 0x20: {
                        printTag("JCOP 31/41");
                        break;
                    }
                    case 0x28: {
                        printTag("JCOP31 or JCOP41 v2.3.1");
                        break;
                    }
                    case 0x38: {
                        printTag("Nokia 6212 or 6131");
                        break;
                    }
                    case 0x98: {
                        printTag("Gemplus MPCOS");
                        break;
                    }
                    default: {
                        break;
                    }
                }
                break;
        }
    }

    // try to request ATS even if tag claims not to support it
    if (select_status == 2) {
        uint8_t rats[] = { 0xE0, 0x80 }; // FSDI=8 (FSD=256), CID=0
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_APPEND_CRC | ISO14A_NO_DISCONNECT, 2, 0, rats, sizeof(rats));
        WaitForResponse(CMD_ACK, &resp);

        memcpy(card.ats, resp.data.asBytes, resp.oldarg[0]);
        card.ats_len = resp.oldarg[0]; // note: ats_len includes CRC Bytes
    }

    if (card.ats_len >= 3) {        // a valid ATS consists of at least the length byte (TL) and 2 CRC bytes

        PrintAndLogEx(INFO, "-------------------------- " _CYAN_("ATS") " --------------------------");
        bool ta1 = 0, tb1 = 0, tc1 = 0;

        if (select_status == 2) {
            PrintAndLogEx(INFO, "--> SAK incorrectly claims that card doesn't support RATS <--");
        }

        if (card.ats[0] != card.ats_len - 2) {
            PrintAndLogEx(WARNING, "ATS may be corrupted. Length of ATS (%d bytes incl. 2 Bytes CRC) doesn't match TL", card.ats_len);
        }

        PrintAndLogEx(SUCCESS, "ATS: " _YELLOW_("%s")"[ %02x %02x ]", sprint_hex(card.ats, card.ats_len - 2), card.ats[card.ats_len - 1], card.ats[card.ats_len]);
        PrintAndLogEx(INFO, "     " _YELLOW_("%02x") "...............  TL    length is " _GREEN_("%d") " bytes", card.ats[0], card.ats[0]);

        if (card.ats[0] > 1) { // there is a format byte (T0)
            ta1 = (card.ats[1] & 0x10) == 0x10;
            tb1 = (card.ats[1] & 0x20) == 0x20;
            tc1 = (card.ats[1] & 0x40) == 0x40;
            int16_t fsci = card.ats[1] & 0x0f;

            PrintAndLogEx(INFO, "        " _YELLOW_("%02X") "............  T0    TA1 is%s present, TB1 is%s present, "
                          "TC1 is%s present, FSCI is %d (FSC = %d)",
                          card.ats[1],
                          (ta1 ? "" : _RED_(" NOT")),
                          (tb1 ? "" : _RED_(" NOT")),
                          (tc1 ? "" : _RED_(" NOT")),
                          fsci,
                          fsci < ARRAYLEN(atsFSC) ? atsFSC[fsci] : -1
                         );
        }
        int pos = 2;
        if (ta1) {
            char dr[16], ds[16];
            dr[0] = ds[0] = '\0';
            if (card.ats[pos] & 0x10) strcat(ds, "2, ");
            if (card.ats[pos] & 0x20) strcat(ds, "4, ");
            if (card.ats[pos] & 0x40) strcat(ds, "8, ");
            if (card.ats[pos] & 0x01) strcat(dr, "2, ");
            if (card.ats[pos] & 0x02) strcat(dr, "4, ");
            if (card.ats[pos] & 0x04) strcat(dr, "8, ");
            if (strlen(ds) != 0) ds[strlen(ds) - 2] = '\0';
            if (strlen(dr) != 0) dr[strlen(dr) - 2] = '\0';
            PrintAndLogEx(INFO, "           " _YELLOW_("%02X") ".........  TA1   different divisors are%s supported, "
                          "DR: [%s], DS: [%s]",
                          card.ats[pos],
                          ((card.ats[pos] & 0x80) ? _RED_(" NOT") : ""),
                          dr,
                          ds
                         );

            pos++;
        }

        if (tb1) {
            uint32_t sfgi = card.ats[pos] & 0x0F;
            uint32_t fwi = card.ats[pos] >> 4;

            PrintAndLogEx(INFO, "              " _YELLOW_("%02X") "......  TB1   SFGI = %d (SFGT = %s%d/fc), FWI = " _YELLOW_("%d") " (FWT = %d/fc)",
                          card.ats[pos],
                          (sfgi),
                          sfgi ? "" : "(not needed) ",
                          sfgi ? (1 << 12) << sfgi : 0,
                          fwi,
                          (1 << 12) << fwi
                         );
            pos++;
        }

        if (tc1) {
            PrintAndLogEx(INFO, "                 " _YELLOW_("%02X") "...  TC1   NAD is%s supported, CID is%s supported",
                          card.ats[pos],
                          (card.ats[pos] & 0x01) ? "" : _RED_(" NOT"),
                          (card.ats[pos] & 0x02) ? "" : _RED_(" NOT")
                         );
            pos++;
        }

        // ATS - Historial bytes and identify based on it
        if (card.ats[0] > pos && card.ats[0] <=  card.ats_len - 2) {
            char tip[60];
            tip[0] = '\0';
            if (card.ats[0] - pos >= 7) {

                snprintf(tip, sizeof(tip), "     ");

                if ((card.sak & 0x70) == 0x40) {  // and no GetVersion()..

                    if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x01\xBC\xD6", 7) == 0) {
                        snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus X 2K/4K (SL3)");

                    } else if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x00\x35\xC7", 7) == 0) {

                        if ((card.atqa[0] & 0x02) == 0x02)
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus S 2K (SL3)");
                        else if ((card.atqa[0] & 0x04) == 0x04)
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus S 4K (SL3)");

                    } else if (memcmp(card.ats + pos, "\xC1\x05\x21\x30\x00\xF6\xD1", 7) == 0) {
                        snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus SE 1K (17pF)");

                    } else if (memcmp(card.ats + pos, "\xC1\x05\x21\x30\x10\xF6\xD1", 7) == 0) {
                        snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus SE 1K (70pF)");
                    }

                } else {  //SAK B4,5,6

                    if ((card.sak & 0x20) == 0x20) {  // and no GetVersion()..


                        if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x01\xBC\xD6", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus X 2K (SL1)");
                        } else if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x00\x35\xC7", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus S 2K (SL1)");
                        } else if (memcmp(card.ats + pos, "\xC1\x05\x21\x30\x00\xF6\xD1", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus SE 1K (17pF)");
                        } else if (memcmp(card.ats + pos, "\xC1\x05\x21\x30\x10\xF6\xD1", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus SE 1K (70pF)");
                        }
                    } else {
                        if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x01\xBC\xD6", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus X 4K (SL1)");
                        } else if (memcmp(card.ats + pos, "\xC1\x05\x2F\x2F\x00\x35\xC7", 7) == 0) {
                            snprintf(tip + strlen(tip), sizeof(tip) - strlen(tip), _GREEN_("%s"), "MIFARE Plus S 4K (SL1)");
                        }
                    }
                }
            }

            uint8_t calen = card.ats[0] - pos;
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(INFO, "-------------------- " _CYAN_("Historical bytes") " --------------------");

            if (card.ats[pos] == 0xC1) {
                PrintAndLogEx(INFO, "    %s%s", sprint_hex(card.ats + pos, calen), tip);
                PrintAndLogEx(SUCCESS, "    C1.....................   Mifare or (multiple) virtual cards of various type");
                PrintAndLogEx(SUCCESS, "       %02x..................   length is " _YELLOW_("%d") " bytes", card.ats[pos + 1], card.ats[pos + 1]);
                switch (card.ats[pos + 2] & 0xf0) {
                    case 0x10:
                        PrintAndLogEx(SUCCESS, "          1x...............   MIFARE DESFire");
                        isMifareDESFire = true;
                        isMifareClassic = false;
                        isMifarePlus = false;
                        break;
                    case 0x20:
                        PrintAndLogEx(SUCCESS, "          2x...............   MIFARE Plus");
                        isMifarePlus = true;
                        isMifareDESFire = false;
                        isMifareClassic = false;
                        break;
                }
                switch (card.ats[pos + 2] & 0x0f) {
                    case 0x00:
                        PrintAndLogEx(SUCCESS, "          x0...............   < 1 kByte");
                        break;
                    case 0x01:
                        PrintAndLogEx(SUCCESS, "          x1...............   1 kByte");
                        break;
                    case 0x02:
                        PrintAndLogEx(SUCCESS, "          x2...............   2 kByte");
                        break;
                    case 0x03:
                        PrintAndLogEx(SUCCESS, "          x3...............   4 kByte");
                        break;
                    case 0x04:
                        PrintAndLogEx(SUCCESS, "          x4...............   8 kByte");
                        break;
                }
                switch (card.ats[pos + 3] & 0xf0) {
                    case 0x00:
                        PrintAndLogEx(SUCCESS, "             0x............   Engineering sample");
                        break;
                    case 0x20:
                        PrintAndLogEx(SUCCESS, "             2x............   Released");
                        break;
                }
                switch (card.ats[pos + 3] & 0x0f) {
                    case 0x00:
                        PrintAndLogEx(SUCCESS, "             x0............   Generation 1");
                        break;
                    case 0x01:
                        PrintAndLogEx(SUCCESS, "             x1............   Generation 2");
                        break;
                    case 0x02:
                        PrintAndLogEx(SUCCESS, "             x2............   Generation 3");
                        break;
                }
                switch (card.ats[pos + 4] & 0x0f) {
                    case 0x00:
                        PrintAndLogEx(SUCCESS, "                x0.........   Only VCSL supported");
                        break;
                    case 0x01:
                        PrintAndLogEx(SUCCESS, "                x1.........   VCS, VCSL, and SVC supported");
                        break;
                    case 0x0E:
                        PrintAndLogEx(SUCCESS, "                xE.........   no VCS command supported");
                        break;
                }
            } else {
                PrintAndLogEx(SUCCESS, "   %s", sprint_hex_inrow(card.ats + pos, calen));
            }
        }

        if (do_aid_search) {


            PrintAndLogEx(INFO, "-------------------- " _CYAN_("AID Search") " --------------------");

            bool found = false;
            int elmindx = 0;
            json_t *root = AIDSearchInit(verbose);
            if (root != NULL) {
                bool ActivateField = true;
                for (elmindx = 0; elmindx < json_array_size(root); elmindx++) {

                    if (kbd_enter_pressed()) {
                        break;
                    }

                    json_t *data = AIDSearchGetElm(root, elmindx);
                    uint8_t vaid[200] = {0};
                    int vaidlen = 0;
                    if (!AIDGetFromElm(data, vaid, sizeof(vaid), &vaidlen) || !vaidlen)
                        continue;

                    uint16_t sw = 0;
                    uint8_t result[1024] = {0};
                    size_t resultlen = 0;
                    int res = EMVSelect(ECC_CONTACTLESS, ActivateField, true, vaid, vaidlen, result, sizeof(result), &resultlen, &sw, NULL);
                    ActivateField = false;
                    if (res)
                        continue;

                    uint8_t dfname[200] = {0};
                    size_t dfnamelen = 0;
                    if (resultlen > 3) {
                        struct tlvdb *tlv = tlvdb_parse_multi(result, resultlen);
                        if (tlv) {
                            // 0x84 Dedicated File (DF) Name
                            const struct tlv *dfnametlv = tlvdb_get_tlv(tlvdb_find_full(tlv, 0x84));
                            if (dfnametlv) {
                                dfnamelen = dfnametlv->len;
                                memcpy(dfname, dfnametlv->value, dfnamelen);
                            }
                            tlvdb_free(tlv);
                        }
                    }

                    if (sw == 0x9000 || sw == 0x6283 || sw == 0x6285) {
                        if (sw == 0x9000) {
                            if (verbose) PrintAndLogEx(SUCCESS, "Application ( " _GREEN_("ok") " )");
                        } else {
                            if (verbose) PrintAndLogEx(WARNING, "Application ( " _RED_("blocked") " )");
                        }

                        PrintAIDDescriptionBuf(root, vaid, vaidlen, verbose);

                        if (dfnamelen) {
                            if (dfnamelen == vaidlen) {
                                if (memcmp(dfname, vaid, vaidlen) == 0) {
                                    if (verbose) PrintAndLogEx(INFO, "(DF) Name found and equal to AID");
                                } else {
                                    PrintAndLogEx(INFO, "(DF) Name not equal to AID: %s :", sprint_hex(dfname, dfnamelen));
                                    PrintAIDDescriptionBuf(root, dfname, dfnamelen, verbose);
                                }
                            } else {
                                PrintAndLogEx(INFO, "(DF) Name not equal to AID: %s :", sprint_hex(dfname, dfnamelen));
                                PrintAIDDescriptionBuf(root, dfname, dfnamelen, verbose);
                            }
                        } else {
                            if (verbose) PrintAndLogEx(INFO, "(DF) Name not found");
                        }

                        if (verbose) PrintAndLogEx(SUCCESS, "----------------------------------------------------");
                        found = true;
                    }

                }
                DropField();
                if (verbose == false && found)
                    PrintAndLogEx(INFO, "----------------------------------------------------");
            }
        }
    } else {
        PrintAndLogEx(INFO, "proprietary non iso14443-4 card found, RATS not supported");
        if ((card.sak & 0x20) == 0x20) {
            PrintAndLogEx(INFO, "--> SAK incorrectly claims that card supports RATS <--");
        }
    }

    int isMagic = 0;
    if (isMifareClassic) {
        isMagic = detect_mf_magic(true);
    }
    if (isMifareUltralight) {
        isMagic = (detect_mf_magic(false) == MAGIC_NTAG21X);
    }
    if (isMifareClassic) {
        int res = detect_classic_static_nonce();
        if (res == NONCE_STATIC)
            PrintAndLogEx(SUCCESS, "Static nonce: " _YELLOW_("yes"));

        if (res == NONCE_FAIL && verbose)
            PrintAndLogEx(SUCCESS, "Static nonce:  " _RED_("read failed"));

        if (res == NONCE_NORMAL) {

            // not static
            res = detect_classic_prng();
            if (res == 1)
                PrintAndLogEx(SUCCESS, "Prng detection: " _GREEN_("weak"));
            else if (res == 0)
                PrintAndLogEx(SUCCESS, "Prng detection: " _YELLOW_("hard"));
            else
                PrintAndLogEx(FAILED, "Prng detection:  " _RED_("fail"));

            if (do_nack_test)
                detect_classic_nackbug(false);
        }
    }

    if (isMifareUltralight)
        PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfu info`"));

    if (isMifarePlus && isMagic == 0)
        PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfp info`"));

    if (isMifareDESFire && isMagic == 0)
        PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfdes info`"));

    if (isST)
        PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf st info`"));

    PrintAndLogEx(NORMAL, "");
    DropField();
    return select_status;
}

static uint16_t get_sw(uint8_t *d, uint8_t n) {
    if (n < 2) {
        return 0;
    }
    n -= 2;
    return d[n] * 0x0100 + d[n + 1];
}

static uint64_t inc_sw_error_occurence(uint16_t sw, uint64_t all_sw[256][256]) {
    uint8_t sw1 = (uint8_t)(sw >> 8);
    uint8_t sw2 = (uint8_t)(0xff & sw);
    if (sw1 == 0x90 && sw2 == 0x00) {
        return 0; // Don't count successes.
    }
    if (sw1 == 0x6d && sw2 == 0x00) {
        return 0xffffffffffffffffULL; // Always max "Instruction not supported".
    }
    return ++all_sw[sw1][sw2];
}

static int CmdHf14AFindapdu(const char *Cmd) {
    // TODO: Option to select AID/File (and skip INS 0xA4).
    // TODO: Check all instructions with extended APDUs if the card support it.
    // TODO: Option to reset tag before every command.
    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf 14a apdufind",
                  "Enumerate APDU's of ISO7816 protocol to find valid CLS/INS/P1/P2 commands.\n"
                  "It loops all 256 possible values for each byte.\n"
                  "The loop oder is INS -> P1/P2 (alternating) -> CLA.\n"
                  "Tag must be on antenna before running.",
                  "hf 14a apdufind\n"
                  "hf 14a apdufind --cla 80\n"
                  "hf 14a apdufind --cla 80 --error-limit 20 --skip-ins a4 --skip-ins b0 --with-le\n"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("c",  "cla",           "<hex>",    "Start value of CLASS (1 hex byte)"),
        arg_str0("i",  "ins",           "<hex>",    "Start value of INSTRUCTION (1 hex byte)"),
        arg_str0(NULL, "p1",            "<hex>",    "Start value of P1 (1 hex byte)"),
        arg_str0(NULL, "p2",            "<hex>",    "Start value of P2 (1 hex byte)"),
        arg_u64_0("r", "reset",         "<number>", "Minimum secondes before resetting the tag (to prevent timeout issues). Default is 5 minutes"),
        arg_u64_0("e", "error-limit",   "<number>", "Maximum times an status word other than 0x9000 or 0x6D00 is shown. Default is 512."),
        arg_strx0("s", "skip-ins",      "<hex>",    "Do not test an instructions (can be specifed multiple times)"),
        arg_lit0("l",  "with-le",                   "Serach  for APDUs with Le=0 (case 2S) as well"),
        arg_lit0("v",  "verbose",                   "Verbose output"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    int cla_len = 0;
    uint8_t cla_arg[1] = {0};
    CLIGetHexWithReturn(ctx, 1, cla_arg, &cla_len);
    int ins_len = 0;
    uint8_t ins_arg[1] = {0};
    CLIGetHexWithReturn(ctx, 2, ins_arg, &ins_len);
    int p1_len = 0;
    uint8_t p1_arg[1] = {0};
    CLIGetHexWithReturn(ctx, 3, p1_arg, &p1_len);
    int p2_len = 0;
    uint8_t p2_arg[1] = {0};
    CLIGetHexWithReturn(ctx, 4, p2_arg, &p2_len);
    uint64_t reset_time = arg_get_u64_def(ctx, 5, 5 * 60);
    uint64_t error_limit = arg_get_u64_def(ctx, 6, 512);
    int ignore_ins_len = 0;
    uint8_t ignore_ins_arg[250] = {0};
    CLIGetHexWithReturn(ctx, 7, ignore_ins_arg, &ignore_ins_len);
    bool with_le = arg_get_lit(ctx, 8);
    bool verbose = arg_get_lit(ctx, 9);

    CLIParserFree(ctx);

    bool activate_field = true;
    bool keep_field_on = true;
    uint8_t cla = cla_arg[0];
    uint8_t ins = ins_arg[0];
    uint8_t p1 = p1_arg[0];
    uint8_t p2 = p2_arg[0];
    uint8_t response[PM3_CMD_DATA_SIZE];
    int response_n = 0;
    uint8_t aSELECT_AID[80];
    int aSELECT_AID_n = 0;

    // Check if the tag reponds to APDUs.
    PrintAndLogEx(INFO, "Sending a test APDU (select file command) to check if the tag is responding to APDU");
    param_gethex_to_eol("00a404000aa000000440000101000100", 0, aSELECT_AID, sizeof(aSELECT_AID), &aSELECT_AID_n);
    int res = ExchangeAPDU14a(aSELECT_AID, aSELECT_AID_n, true, false, response, sizeof(response), &response_n);
    if (res) {
        PrintAndLogEx(FAILED, "Tag did not responde to a test APDU (select file command). Aborting");
        return res;
    }
    PrintAndLogEx(SUCCESS, "Got response. Starting the APDU finder [ CLA " _GREEN_("%02X") " INS " _GREEN_("%02X") " P1 " _GREEN_("%02X") " P2 " _GREEN_("%02X") " ]", cla, ins, p1, p2);
    PrintAndLogEx(INFO, "Press " _GREEN_("<Enter>") " to exit");

    bool inc_p1 = true;
    bool skip_ins = false;
    uint64_t all_sw[256][256] = {0};
    uint64_t sw_occurences = 0;
    uint64_t t_start = msclock();
    uint64_t t_last_reset = msclock();

    // Enumerate APDUs.
    do {
        do {
            do {
retry_ins:
                // Exit (was the Enter key pressed)?
                if (kbd_enter_pressed()) {
                    PrintAndLogEx(INFO, "User interrupted detected. Aborting");
                    goto out;
                }

                // Skip/Ignore this instrctuion?
                for (int i = 0; i < ignore_ins_len; i++) {
                    if (ins == ignore_ins_arg[i]) {
                        skip_ins = true;
                        break;
                    }
                }
                if (skip_ins) {
                    skip_ins = false;
                    continue;
                }

                if (verbose) {
                    PrintAndLogEx(INFO, "Status: [ CLA " _GREEN_("%02X") " INS " _GREEN_("%02X") " P1 " _GREEN_("%02X") " P2 " _GREEN_("%02X") " ]", cla, ins, p1, p2);
                }

                // Send APDU without Le (case 1) and with Le = 0 (case 2S), if "with-le" was set.
                uint8_t command[5] = {cla, ins, p1, p2, 0x00};
                int command_n = 4;
                for (int i = 0; i < 1 + with_le; i++) {
                    // Send APDU.
                    res = ExchangeAPDU14a(command, command_n + i, activate_field, keep_field_on, response, sizeof(response), &response_n);
                    if (res) {
                        DropField();
                        activate_field = true;
                        goto retry_ins;
                    }
                    uint16_t sw = get_sw(response, response_n);
                    sw_occurences = inc_sw_error_occurence(sw, all_sw);

                    // Show response.
                    if (sw_occurences < error_limit) {
                        logLevel_t log_level = INFO;
                        if (sw == 0x9000) {
                            log_level = SUCCESS;
                        }
                        PrintAndLogEx(log_level, "Got response for APDU \"%s\": %04X (%s)", sprint_hex_inrow(command, command_n + i),
                                      sw, GetAPDUCodeDescription(sw >> 8, sw & 0xff));
                        if (response_n > 2) {
                            PrintAndLogEx(SUCCESS, "Response data is: %s | %s", sprint_hex_inrow(response, response_n - 2),
                                          sprint_ascii(response, response_n - 2));
                        }
                    }
                }
                activate_field = false; // Do not reativate the filed until the next reset.
            } while (++ins != ins_arg[0]);
            // Increment P1/P2 in an alternating fashion.
            if (inc_p1) {
                p1++;
            } else {
                p2++;
            }
            inc_p1 = !inc_p1;
            // Check if re-selecting the card is needed.
            uint64_t t_since_last_reset = ((msclock() - t_last_reset) / 1000);
            if (t_since_last_reset > reset_time) {
                DropField();
                activate_field = true;
                t_last_reset = msclock();
                PrintAndLogEx(INFO, "Last reset was %" PRIu64 " seconds ago. Reseting the tag to prevent timeout issues", t_since_last_reset);
            }
            PrintAndLogEx(INFO, "Status: [ CLA " _GREEN_("%02X") " INS " _GREEN_("%02X") " P1 " _GREEN_("%02X") " P2 " _GREEN_("%02X") " ]", cla, ins, p1, p2);
        } while (p1 != p1_arg[0] || p2 != p2_arg[0]);
        cla++;
        PrintAndLogEx(INFO, "Status: [ CLA " _GREEN_("%02X") " INS " _GREEN_("%02X") " P1 " _GREEN_("%02X") " P2 " _GREEN_("%02X") " ]", cla, ins, p1, p2);
    } while (cla != cla_arg[0]);

out:
    PrintAndLogEx(SUCCESS, "Runtime: %" PRIu64 " seconds\n", (msclock() - t_start) / 1000);
    DropField();
    return PM3_SUCCESS;
}

static command_t CommandTable[] = {
    {"help",        CmdHelp,              AlwaysAvailable, "This help"},
    {"list",        CmdHF14AList,         AlwaysAvailable,  "List ISO 14443-a history"},
    {"info",        CmdHF14AInfo,         IfPm3Iso14443a,  "Tag information"},
    {"reader",      CmdHF14AReader,       IfPm3Iso14443a,  "Act like an ISO14443-a reader"},
    {"cuids",       CmdHF14ACUIDs,        IfPm3Iso14443a,  "<n> Collect n>0 ISO14443-a UIDs in one go"},
    {"sim",         CmdHF14ASim,          IfPm3Iso14443a,  "<UID> -- Simulate ISO 14443-a tag"},
    {"sniff",       CmdHF14ASniff,        IfPm3Iso14443a,  "sniff ISO 14443-a traffic"},
    {"apdu",        CmdHF14AAPDU,         IfPm3Iso14443a,  "Send ISO 14443-4 APDU to tag"},
    {"chaining",    CmdHF14AChaining,     IfPm3Iso14443a,  "Control ISO 14443-4 input chaining"},
    {"raw",         CmdHF14ACmdRaw,       IfPm3Iso14443a,  "Send raw hex data to tag"},
    {"antifuzz",    CmdHF14AAntiFuzz,     IfPm3Iso14443a,  "Fuzzing the anticollision phase.  Warning! Readers may react strange"},
    {"config",      CmdHf14AConfig,       IfPm3Iso14443a,  "Configure 14a settings (use with caution)"},
    {"apdufind",    CmdHf14AFindapdu,     IfPm3Iso14443a,  "Enuerate APDUs - CLA/INS/P1P2"},
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdHF14A(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}
