//-----------------------------------------------------------------------------
// Ultralight Code (c) 2013,2014 Midnitesnake & Andy Davies of Pentura
// 2015,2016,2017 Iceman, Marshmellow
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// High frequency MIFARE ULTRALIGHT (C) commands
//-----------------------------------------------------------------------------
#include "cmdhfmfu.h"
#include <ctype.h>
#include "cmdparser.h"
#include "commonutil.h"
#include "crypto/libpcrypto.h"
#include "des.h"
#include "aes.h"
#include "cmdhfmf.h"
#include "cmdhf14a.h"
#include "comms.h"
#include "fileutils.h"
#include "protocols.h"
#include "generator.h"
#include "mifare/ndef.h"
#include "cliparser.h"
#include "cmdmain.h"

#define MAX_UL_BLOCKS       0x0F
#define MAX_ULC_BLOCKS      0x2F
#define MAX_ULEV1a_BLOCKS   0x13
#define MAX_ULEV1b_BLOCKS   0x28
#define MAX_NTAG_203        0x29
#define MAX_NTAG_210        0x13
#define MAX_NTAG_212        0x28
#define MAX_NTAG_213        0x2C
#define MAX_NTAG_215        0x86
#define MAX_NTAG_216        0xE6
#define MAX_NTAG_I2C_1K     0xE9
#define MAX_MY_D_NFC        0xFF
#define MAX_MY_D_MOVE       0x25
#define MAX_MY_D_MOVE_LEAN  0x0F
#define MAX_UL_NANO_40      0x0A

static int CmdHelp(const char *Cmd);

static int usage_hf_mfu_restore(void) {
    PrintAndLogEx(NORMAL, "Restore dumpfile onto card.");
    PrintAndLogEx(NORMAL, "Usage:  hf mfu restore [h] [l] [s] k <key> n <filename w .bin> ");
    PrintAndLogEx(NORMAL, "  Options :");
    PrintAndLogEx(NORMAL, "  k <key> : (optional) key for authentication [UL-C 16bytes, EV1/NTAG 4bytes]");
    PrintAndLogEx(NORMAL, "  l       : (optional) swap entered key's endianness");
    PrintAndLogEx(NORMAL, "  s       : (optional) enable special write UID " _BLUE_("-MAGIC TAG ONLY-"));
    PrintAndLogEx(NORMAL, "  e       : (optional) enable special write version/signature " _BLUE_("-MAGIC NTAG 21* ONLY-"));
    PrintAndLogEx(NORMAL, "  r       : (optional) use the password found in dumpfile to configure tag. requires " _YELLOW_("'e'") " parameter to work");
    PrintAndLogEx(NORMAL, "  f <fn>  : " _YELLOW_("filename w .bin") " to restore");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("       hf mfu restore s f myfile"));
    PrintAndLogEx(NORMAL, _YELLOW_("       hf mfu restore k AABBCCDD s f myfile"));
    PrintAndLogEx(NORMAL, _YELLOW_("       hf mfu restore k AABBCCDD s e r f myfile"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_eload(void) {
    PrintAndLogEx(NORMAL, "It loads emul dump from the file " _YELLOW_("`filename.eml`"));
    PrintAndLogEx(NORMAL, "Hint: See " _YELLOW_("`script run hf_mfu_dumptoemulator`") " to convert the .bin to the eml");
    PrintAndLogEx(NORMAL, "Usage:  hf mfu eload u <file name w/o `.eml`> [numblocks]");
    PrintAndLogEx(NORMAL, "  Options:");
    PrintAndLogEx(NORMAL, "    h          : this help");
    PrintAndLogEx(NORMAL, "    u          : UL (required)");
    PrintAndLogEx(NORMAL, "    [filename] : without `.eml` (required)");
    PrintAndLogEx(NORMAL, "    numblocks  : number of blocks to load from eml file (optional)");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu eload u filename"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu eload u filename 57"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_sim(void) {
    PrintAndLogEx(NORMAL, "\nEmulating Ultralight tag from emulator memory\n");
    PrintAndLogEx(NORMAL, "\nBe sure to load the emulator memory first!\n");
    PrintAndLogEx(NORMAL, "Usage: hf mfu sim t 7 u <uid> [n <num>]");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h       : this help");
    PrintAndLogEx(NORMAL, "    t 7     : 7 = NTAG or Ultralight sim (required)");
    PrintAndLogEx(NORMAL, "    n <num> : exit simulation after <num> blocks have been read by reader. 0 = infinite (optional)");
    PrintAndLogEx(NORMAL, "    u <uid> : 4 or 7 byte UID (optional)");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu sim t 7"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu sim t 7 u 1122344556677"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu sim t 7 u 1122344556677 n 5"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_ucauth(void) {
    PrintAndLogEx(NORMAL, "Tests 3DES password on Mifare Ultralight-C tag.");
    PrintAndLogEx(NORMAL, "If password is not specified, a set of known defaults will be tested.");
    PrintAndLogEx(NORMAL, "Usage:  hf mfu cauth [k] <password (32 hex symbols)>");
    PrintAndLogEx(NORMAL, "       k          - keep field on (only if a password is provided too)");
    PrintAndLogEx(NORMAL, "       [password] - (32 hex symbols)");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("       hf mfu cauth"));
    PrintAndLogEx(NORMAL, _YELLOW_("       hf mfu cauth 000102030405060708090a0b0c0d0e0f"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_ucsetpwd(void) {
    PrintAndLogEx(NORMAL, "Set 3DES password on Mifare Ultralight-C tag.");
    PrintAndLogEx(NORMAL, "Usage:  hf mfu setpwd <password (32 hex symbols)>");
    PrintAndLogEx(NORMAL, "       [password] - (32 hex symbols)");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("         hf mfu setpwd 000102030405060708090a0b0c0d0e0f"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_ucsetuid(void) {
    PrintAndLogEx(NORMAL, "Usage:  hf mfu setuid <uid (14 hex symbols)>");
    PrintAndLogEx(NORMAL, "       [uid] - (14 hex symbols)");
    PrintAndLogEx(NORMAL, "\n");
    PrintAndLogEx(NORMAL, "This only works for " _BLUE_("Magic Ultralight") " tags.");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("         hf mfu setuid 11223344556677"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_gendiverse(void) {
    PrintAndLogEx(NORMAL, "Usage:  hf mfu gen [h] [r] <uid (8 hex symbols)>");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h       : this help");
    PrintAndLogEx(NORMAL, "    r       : read uid from tag");
    PrintAndLogEx(NORMAL, "    <uid>   : 4 byte UID (optional)");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu gen r"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu gen 11223344"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_pwdgen(void) {
    PrintAndLogEx(NORMAL, "Usage:  hf mfu pwdgen [h|t] [r] <uid (14 hex symbols)>");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "    h       : this help");
    PrintAndLogEx(NORMAL, "    t       : selftest");
    PrintAndLogEx(NORMAL, "    r       : read uid from tag");
    PrintAndLogEx(NORMAL, "    <uid>   : 7 byte UID (optional)");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu pwdgen r"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu pwdgen 11223344556677"));
    PrintAndLogEx(NORMAL, _YELLOW_("        hf mfu pwdgen t"));
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

static int usage_hf_mfu_otp_tearoff(void) {
    PrintAndLogEx(NORMAL, "Tear-off test against OTP block (no 3) on MFU tags - More help sooner or later\n");
    PrintAndLogEx(NORMAL, "Usage:  hf mfu otptear b <block number> i <intervalTime> l <limitTime> s <startTime> d <data before> t <data after>\n");
    PrintAndLogEx(NORMAL, "Options:");
    PrintAndLogEx(NORMAL, "  b <no>    : (optional) block to run the test -  default block: 8 (not OTP for safety)");
    PrintAndLogEx(NORMAL, "  i <time>  : (optional) time interval to increase in each test - default 500 us");
    PrintAndLogEx(NORMAL, "  l <time>  : (optional) limit time to run the test - default 3000 us");
    PrintAndLogEx(NORMAL, "  s <time>  : (optional) start time to run the test - default 0 us");
    PrintAndLogEx(NORMAL, "  d <data>  : (optional) data to full-write before trying the OTP test - default 0x00");
    PrintAndLogEx(NORMAL, "  t <data>  : (optional) data to write while running the OTP test - default 0x00");
    PrintAndLogEx(NORMAL, "  m <data>  : (optional) exit criteria, if block matches this value");
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(NORMAL, "Examples:");
    PrintAndLogEx(NORMAL, "        hf mfu otptear b 3");
    PrintAndLogEx(NORMAL, "        hf mfu otptear b 8 i 100 l 3000 s 1000");
    PrintAndLogEx(NORMAL, "        hf mfu otptear b 3 i 1 l 200");
    PrintAndLogEx(NORMAL, "        hf mfu otptear b 3 i 100 l 2500 s 200 d FFFFFFFF t EEEEEEEE");
    PrintAndLogEx(NORMAL, "        hf mfu otptear b 3 i 100 l 2500 s 200 d FFFFFFFF t EEEEEEEE m 00000000    -> such quite when OTP is reset");
    return PM3_SUCCESS;
}


uint8_t default_3des_keys[][16] = {
    { 0x42, 0x52, 0x45, 0x41, 0x4b, 0x4d, 0x45, 0x49, 0x46, 0x59, 0x4f, 0x55, 0x43, 0x41, 0x4e, 0x21 }, // 3des std key
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // all zeroes
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f }, // 0x00-0x0F
    { 0x49, 0x45, 0x4D, 0x4B, 0x41, 0x45, 0x52, 0x42, 0x21, 0x4E, 0x41, 0x43, 0x55, 0x4F, 0x59, 0x46 }, // NFC-key
    { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 }, // all ones
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, // all FF
    { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } // 11 22 33
};

uint8_t default_pwd_pack[][4] = {
    {0xFF, 0xFF, 0xFF, 0xFF}, // PACK 0x00,0x00 -- factory default
};

uint32_t UL_TYPES_ARRAY[] = {
    UNKNOWN,   UL,          UL_C,        UL_EV1_48,       UL_EV1_128,      NTAG,
    NTAG_203,  NTAG_210,    NTAG_212,    NTAG_213,        NTAG_215,        NTAG_216,
    MY_D,      MY_D_NFC,    MY_D_MOVE,   MY_D_MOVE_NFC,   MY_D_MOVE_LEAN,  FUDAN_UL,
    UL_EV1,    NTAG_213_F,  NTAG_216_F,  UL_NANO_40,      NTAG_I2C_1K,     NTAG_213_TT,
    NTAG_213_C
};

uint8_t UL_MEMORY_ARRAY[ARRAYLEN(UL_TYPES_ARRAY)] = {
    MAX_UL_BLOCKS,     MAX_UL_BLOCKS, MAX_ULC_BLOCKS, MAX_ULEV1a_BLOCKS, MAX_ULEV1b_BLOCKS,  MAX_NTAG_203,
    MAX_NTAG_203,      MAX_NTAG_210,  MAX_NTAG_212,   MAX_NTAG_213,      MAX_NTAG_215,       MAX_NTAG_216,
    MAX_UL_BLOCKS,     MAX_MY_D_NFC,  MAX_MY_D_MOVE,  MAX_MY_D_MOVE,     MAX_MY_D_MOVE_LEAN, MAX_UL_BLOCKS,
    MAX_ULEV1a_BLOCKS, MAX_NTAG_213,  MAX_NTAG_216,   MAX_UL_NANO_40,    MAX_NTAG_I2C_1K,    MAX_NTAG_213,
    MAX_NTAG_213
};

//------------------------------------
// get version nxp product type
static char *getProductTypeStr(uint8_t id) {

    static char buf[20];
    char *retStr = buf;

    switch (id) {
        case 3:
            sprintf(retStr, "%02X, Ultralight", id);
            break;
        case 4:
            sprintf(retStr, "%02X, NTAG", id);
            break;
        default:
            sprintf(retStr, "%02X, unknown", id);
            break;
    }
    return buf;
}

/*
  The 7 MSBits (=n) code the storage size itself based on 2^n,
  the LSBit is set to '0' if the size is exactly 2^n
  and set to '1' if the storage size is between 2^n and 2^(n+1).
*/
static char *getUlev1CardSizeStr(uint8_t fsize) {

    static char buf[40];
    char *retStr = buf;
    memset(buf, 0, sizeof(buf));

    uint16_t usize = 1 << ((fsize >> 1) + 1);
    uint16_t lsize = 1 << (fsize >> 1);

    // is  LSB set?
    if (fsize & 1)
        sprintf(retStr, "%02X, (%u <-> %u bytes)", fsize, usize, lsize);
    else
        sprintf(retStr, "%02X, (%u bytes)", fsize, lsize);
    return buf;
}

static void ul_switch_on_field(void) {
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_DISCONNECT | ISO14A_NO_RATS, 0, 0, NULL, 0);
}

static int ul_send_cmd_raw(uint8_t *cmd, uint8_t cmdlen, uint8_t *response, uint16_t responseLength) {
    clearCommandBuffer();
    SendCommandOLD(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_NO_DISCONNECT | ISO14A_APPEND_CRC | ISO14A_NO_RATS, cmdlen, 0, cmd, cmdlen);
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) return -1;
    if (!resp.oldarg[0] && responseLength) return -1;

    uint16_t resplen = (resp.oldarg[0] < responseLength) ? resp.oldarg[0] : responseLength;
    memcpy(response, resp.data.asBytes, resplen);
    return resplen;
}

static bool ul_select(iso14a_card_select_t *card) {

    ul_switch_on_field();

    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500) == false) {
        PrintAndLogEx(WARNING, "timeout while waiting for reply.");
        DropField();
        return false;
    } else {

        uint16_t len = (resp.oldarg[1] & 0xFFFF);
        if (len == 0) {
            PrintAndLogEx(WARNING, "iso14443a card select failed");
            DropField();
            return false;
        }

        if (card)
            memcpy(card, resp.data.asBytes, sizeof(iso14a_card_select_t));
    }
    return true;
}

// This read command will at least return 16bytes.
static int ul_read(uint8_t page, uint8_t *response, uint16_t responseLength) {

    uint8_t cmd[] = {ISO14443A_CMD_READBLOCK, page};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), response, responseLength);
    return len;
}

static int ul_comp_write(uint8_t page, uint8_t *data, uint8_t datalen) {

    if (data == NULL)
        return -1;

    uint8_t cmd[18];
    memset(cmd, 0x00, sizeof(cmd));
    datalen = (datalen > 16) ? 16 : datalen;

    cmd[0] = ISO14443A_CMD_WRITEBLOCK;
    cmd[1] = page;
    memcpy(cmd + 2, data, datalen);

    uint8_t response[1] = {0xFF};
    ul_send_cmd_raw(cmd, 2 + datalen, response, sizeof(response));
    // ACK
    if (response[0] == 0x0a) return 0;
    // NACK
    return -1;
}

static int ulc_requestAuthentication(uint8_t *nonce, uint16_t nonceLength) {

    uint8_t cmd[] = {MIFARE_ULC_AUTH_1, 0x00};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), nonce, nonceLength);
    return len;
}

static int ulc_authentication(uint8_t *key, bool switch_off_field) {

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREUC_AUTH, switch_off_field, 0, 0, key, 16);
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) return 0;
    if (resp.oldarg[0] == 1) return 1;

    return 0;
}

static int try_default_3des_keys(uint8_t **correct_key) {
    PrintAndLogEx(INFO, "Trying some default 3des keys");
    for (uint8_t i = 0; i < ARRAYLEN(default_3des_keys); ++i) {
        uint8_t *key = default_3des_keys[i];
        if (ulc_authentication(key, true)) {
            *correct_key = key;
            return 1;
        }
    }
    return 0;
}

static int ulev1_requestAuthentication(uint8_t *pwd, uint8_t *pack, uint16_t packLength) {

    uint8_t cmd[] = {MIFARE_ULEV1_AUTH, pwd[0], pwd[1], pwd[2], pwd[3]};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), pack, packLength);
    // NACK tables different tags,  but between 0-9 is a NEGATIVE response.
    // ACK == 0xA
    if (len == 1 && pack[0] <= 0x09)
        return -1;
    return len;
}

static int ul_auth_select(iso14a_card_select_t *card, TagTypeUL_t tagtype, bool hasAuthKey, uint8_t *authkey, uint8_t *pack, uint8_t packSize) {
    if (hasAuthKey && (tagtype & UL_C)) {
        //will select card automatically and close connection on error
        if (!ulc_authentication(authkey, false)) {
            PrintAndLogEx(WARNING, "Authentication Failed UL-C");
            return PM3_ESOFT;
        }
    } else {
        if (!ul_select(card)) return PM3_ESOFT;

        if (hasAuthKey) {
            if (ulev1_requestAuthentication(authkey, pack, packSize) == -1) {
                DropField();
                PrintAndLogEx(WARNING, "Authentication Failed UL-EV1/NTAG");
                return PM3_ESOFT;
            }
        }
    }
    return PM3_SUCCESS;
}

static int ulev1_getVersion(uint8_t *response, uint16_t responseLength) {
    uint8_t cmd[] = {MIFARE_ULEV1_VERSION};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), response, responseLength);
    return len;
}

static int ulev1_readCounter(uint8_t counter, uint8_t *response, uint16_t responseLength) {

    uint8_t cmd[] = {MIFARE_ULEV1_READ_CNT, counter};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), response, responseLength);
    return len;
}

static int ulev1_readTearing(uint8_t counter, uint8_t *response, uint16_t responseLength) {

    uint8_t cmd[] = {MIFARE_ULEV1_CHECKTEAR, counter};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), response, responseLength);
    return len;
}

static int ulev1_readSignature(uint8_t *response, uint16_t responseLength) {

    uint8_t cmd[] = {MIFARE_ULEV1_READSIG, 0x00};
    int len = ul_send_cmd_raw(cmd, sizeof(cmd), response, responseLength);
    return len;
}

// Fudan check checks for which error is given for a command with incorrect crc
// NXP UL chip responds with 01, fudan 00.
// other possible checks:
//  send a0 + crc
//  UL responds with 00, fudan doesn't respond
//  or
//  send a200 + crc
//  UL doesn't respond, fudan responds with 00
//  or
//  send 300000 + crc (read with extra byte(s))
//  UL responds with read of page 0, fudan doesn't respond.
//
// make sure field is off before calling this function
static int ul_fudan_check(void) {
    iso14a_card_select_t card;
    if (!ul_select(&card))
        return UL_ERROR;

    uint8_t cmd[4] = {0x30, 0x00, 0x02, 0xa7}; //wrong crc on purpose  should be 0xa8
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_RAW | ISO14A_NO_DISCONNECT | ISO14A_NO_RATS, 4, 0, cmd, sizeof(cmd));
    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) return UL_ERROR;
    if (resp.oldarg[0] != 1) return UL_ERROR;

    return (!resp.data.asBytes[0]) ? FUDAN_UL : UL; //if response == 0x00 then Fudan, else Genuine NXP
}

static int ul_print_default(uint8_t *data, uint8_t *real_uid) {

    uint8_t uid[7];
    uid[0] = data[0];
    uid[1] = data[1];
    uid[2] = data[2];
    uid[3] = data[4];
    uid[4] = data[5];
    uid[5] = data[6];
    uid[6] = data[7];
    bool mful_uid_layout = true;

    if (memcmp(uid, real_uid, 7) != 0) {
        mful_uid_layout = false;
    }
    PrintAndLogEx(SUCCESS, "       UID: " _GREEN_("%s"), sprint_hex(real_uid, 7));
    PrintAndLogEx(SUCCESS, "    UID[0]: %02X, %s",  real_uid[0], getTagInfo(real_uid[0]));
    if (real_uid[0] == 0x05 && ((real_uid[1] & 0xf0) >> 4) == 2) {   // is infineon and 66RxxP
        uint8_t chip = (data[8] & 0xC7); // 11000111  mask, bit 3,4,5 RFU
        switch (chip) {
            case 0xC2:
                PrintAndLogEx(SUCCESS, "   IC type: SLE 66R04P 770 Bytes");
                break; //77 pages
            case 0xC4:
                PrintAndLogEx(SUCCESS, "   IC type: SLE 66R16P 2560 Bytes");
                break; //256 pages
            case 0xC6:
                PrintAndLogEx(SUCCESS, "   IC type: SLE 66R32P 5120 Bytes");
                break; //512 pages /2 sectors
        }
    }
    if (mful_uid_layout) {
        // CT (cascade tag byte) 0x88 xor SN0 xor SN1 xor SN2
        int crc0 = 0x88 ^ uid[0] ^ uid[1] ^ uid[2];
        if (data[3] == crc0)
            PrintAndLogEx(SUCCESS, "      BCC0: %02X (" _GREEN_("ok") ")", data[3]);
        else
            PrintAndLogEx(NORMAL, "      BCC0: %02X, crc should be %02X", data[3], crc0);

        int crc1 = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];
        if (data[8] == crc1)
            PrintAndLogEx(SUCCESS, "      BCC1: %02X (" _GREEN_("ok") ")", data[8]);
        else
            PrintAndLogEx(NORMAL, "      BCC1: %02X, crc should be %02X", data[8], crc1);
        PrintAndLogEx(SUCCESS, "  Internal: %02X (%s)", data[9], (data[9] == 0x48) ? _GREEN_("default") : _RED_("not default"));
    } else {
        PrintAndLogEx(SUCCESS, "Blocks 0-2: %s", sprint_hex(data + 0, 12));
    }

    PrintAndLogEx(SUCCESS, "      Lock: %s - %s",
                  sprint_hex(data + 10, 2),
                  sprint_bin(data + 10, 2)
                 );

    PrintAndLogEx(SUCCESS, "OneTimePad: %s - %s",
                  sprint_hex(data + 12, 4),
                  sprint_bin(data + 12, 4)
                 );

    return PM3_SUCCESS;
}

static int ndef_get_maxsize(uint8_t *data) {
    // no NDEF message
    if (data[0] != 0xE1)
        return 0;

    if (data[2] == 0x06)
        return 48;
    else if (data[2] == 0x12)
        return 144;
    else if (data[2] == 0x3E)
        return 496;
    else if (data[2] == 0x6D)
        return 872;
    return 0;
}

static int ndef_print_CC(uint8_t *data) {
    // no NDEF message
    if (data[0] != 0xE1)
        return PM3_ESOFT;

//NFC Forum Type 1,2,3,4
//
// 4 has 1.1 (11)

// b7, b6 major version
// b5, b4 minor version
// b3, b2 read
// 00 always, 01 rfu, 10 proprietary, 11 rfu
// b1, b0 write
// 00 always, 01 rfo, 10 proprietary, 11 never
    uint8_t cc_write = data[1] & 0x03;
    uint8_t cc_read  = (data[1] & 0x0C) >> 2;
    uint8_t cc_minor = (data[1] & 0x30) >> 4;
    uint8_t cc_major = (data[1] & 0xC0) >> 6;

    char wStr[50];
    switch (cc_write) {
        case 0:
            sprintf(wStr, "Write access granted without any security");
            break;
        case 1:
            sprintf(wStr, "RFU");
            break;
        case 2:
            sprintf(wStr, "Proprietary");
            break;
        case 3:
            sprintf(wStr, "No write access");
            break;
    }
    char rStr[46];
    switch (cc_read) {
        case 0:
            sprintf(rStr, "Read access granted without any security");
            break;
        case 1:
        case 3:
            sprintf(rStr, "RFU");
            break;
        case 2:
            sprintf(rStr, "Proprietary");
            break;
    }


    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("NDEF Message"));
    PrintAndLogEx(SUCCESS, "Capability Container: %s", sprint_hex(data, 4));
    PrintAndLogEx(SUCCESS, "  %02X: NDEF Magic Number", data[0]);

//    PrintAndLogEx(SUCCESS, "  %02X : version %d.%d supported by tag", data[1], (data[1] & 0xF0) >> 4, data[1] & 0x0F);
    PrintAndLogEx(SUCCESS, "  %02X: version %d.%d supported by tag", data[1], cc_major, cc_minor);
    PrintAndLogEx(SUCCESS, "       : %s / %s", rStr, wStr);

    PrintAndLogEx(SUCCESS, "  %02X: Physical Memory Size: %d bytes", data[2], data[2] * 8);
    if (data[2] == 0x06)
        PrintAndLogEx(SUCCESS, "  %02X: NDEF Memory Size: %d bytes", data[2], 48);
    else if (data[2] == 0x12)
        PrintAndLogEx(SUCCESS, "  %02X: NDEF Memory Size: %d bytes", data[2], 144);
    else if (data[2] == 0x3E)
        PrintAndLogEx(SUCCESS, "  %02X: NDEF Memory Size: %d bytes", data[2], 496);
    else if (data[2] == 0x6D)
        PrintAndLogEx(SUCCESS, "  %02X: NDEF Memory Size: %d bytes", data[2], 872);

    uint8_t msb3   = (data[3] & 0xE0) >> 5;
    uint8_t sf     = (data[3] & 0x10) >> 4;
    uint8_t lb     = (data[3] & 0x08) >> 3;
    uint8_t mlrule = (data[3] & 0x06) >> 1;
    uint8_t mbread = (data[3] & 0x01);

    PrintAndLogEx(SUCCESS, "  Additional feature information");
    PrintAndLogEx(SUCCESS, "  %02X", data[3]);
    PrintAndLogEx(SUCCESS, "  00000000");
    PrintAndLogEx(SUCCESS, "  xxx      - %02X: RFU (%s)", msb3, (msb3 == 0) ? _GREEN_("ok") : _RED_("fail"));
    PrintAndLogEx(SUCCESS, "     x     - %02X: %s special frame", sf, (sf) ? "support" : "don\'t support");
    PrintAndLogEx(SUCCESS, "      x    - %02X: %s lock block", lb, (lb) ? "support" : "don\'t support");
    PrintAndLogEx(SUCCESS, "       xx  - %02X: RFU (%s)", mlrule, (mlrule == 0) ? _GREEN_("ok") : _RED_("fail"));
    PrintAndLogEx(SUCCESS, "         x - %02X: IC %s multiple block reads", mbread, (mbread) ? "support" : "don\'t support");
    return PM3_SUCCESS;
}

int ul_print_type(uint32_t tagtype, uint8_t spaces) {

    if (spaces > 10)
        spaces = 10;

    char typestr[100];
    memset(typestr, 0x00, sizeof(typestr));

    if (tagtype & UL)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight (MF0ICU1)s"), spaces, "");
    else if (tagtype & UL_C)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight C (MF0ULC)"), spaces, "");
    else if (tagtype & UL_NANO_40)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight Nano 40bytes (MF0UNH00)"), spaces, "");
    else if (tagtype & UL_EV1_48)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight EV1 48bytes (MF0UL1101)"), spaces, "");
    else if (tagtype & UL_EV1_128)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight EV1 128bytes (MF0UL2101)"), spaces, "");
    else if (tagtype & UL_EV1)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("MIFARE Ultralight EV1 UNKNOWN"), spaces, "");
    else if (tagtype & NTAG)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG UNKNOWN"), spaces, "");
    else if (tagtype & NTAG_203)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 203 144bytes (NT2H0301F0DT)"), spaces, "");
    else if (tagtype & NTAG_210)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 210 48bytes (NT2L1011G0DU)"), spaces, "");
    else if (tagtype & NTAG_212)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 212 128bytes (NT2L1211G0DU)"), spaces, "");
    else if (tagtype & NTAG_213)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 213 144bytes (NT2H1311G0DU)"), spaces, "");
    else if (tagtype & NTAG_213_F)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 213F 144bytes (NT2H1311F0DTL)"), spaces, "");
    else if (tagtype & NTAG_213_C)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 213C 144bytes (NT2H1311C1DTL)"), spaces, "");
    else if (tagtype & NTAG_213_TT)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 213TT 144bytes (NT2H1311TTDU)"), spaces, "");
    else if (tagtype & NTAG_215)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 215 504bytes (NT2H1511G0DU)"), spaces, "");
    else if (tagtype & NTAG_216)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 216 888bytes (NT2H1611G0DU)"), spaces, "");
    else if (tagtype & NTAG_216_F)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG 216F 888bytes (NT2H1611F0DTL)"), spaces, "");
    else if (tagtype & NTAG_I2C_1K)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG I2C 888bytes (NT3H1101FHK)"), spaces, "");
    else if (tagtype & NTAG_I2C_2K)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG I2C 1904bytes (NT3H1201FHK)"), spaces, "");
    else if (tagtype & NTAG_I2C_1K_PLUS)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG I2C plus 888bytes (NT3H2111FHK)"), spaces, "");
    else if (tagtype & NTAG_I2C_2K_PLUS)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("NTAG I2C plus 1912bytes (NT3H2211FHK)"), spaces, "");
    else if (tagtype & MY_D)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("INFINEON my-d\x99 (SLE 66RxxS)"), spaces, "");
    else if (tagtype & MY_D_NFC)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("INFINEON my-d\x99 NFC (SLE 66RxxP)"), spaces, "");
    else if (tagtype & MY_D_MOVE)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("INFINEON my-d\x99 move (SLE 66R01P)"), spaces, "");
    else if (tagtype & MY_D_MOVE_NFC)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("INFINEON my-d\x99 move NFC (SLE 66R01P)"), spaces, "");
    else if (tagtype & MY_D_MOVE_LEAN)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("INFINEON my-d\x99 move lean (SLE 66R01L)"), spaces, "");
    else if (tagtype & FUDAN_UL)
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("FUDAN Ultralight Compatible (or other compatible)"), spaces, "");
    else
        snprintf(typestr, sizeof(typestr), "%*sTYPE: " _YELLOW_("Unknown %06x"), spaces, "", tagtype);

    bool ismagic = ((tagtype & MAGIC) == MAGIC);
    if (ismagic)
        snprintf(typestr + strlen(typestr), 4, " (");

    snprintf(typestr + strlen(typestr), sizeof(typestr) - strlen(typestr), " %s ", (tagtype & MAGIC) ?  _GREEN_("magic") : "");
    tagtype ^= MAGIC;
    snprintf(typestr + strlen(typestr), sizeof(typestr) - strlen(typestr), "%s", (tagtype & MAGIC_1A) ? _GREEN_("Gen 1a") : "");
    snprintf(typestr + strlen(typestr), sizeof(typestr) - strlen(typestr), "%s", (tagtype & MAGIC_1B) ? _GREEN_("Gen 1b") : "");

    if (ismagic)
        snprintf(typestr + strlen(typestr), 4, " )");

    PrintAndLogEx(SUCCESS, "%s", typestr);
    return PM3_SUCCESS;
}

static int ulc_print_3deskey(uint8_t *data) {
    PrintAndLogEx(NORMAL, "         deskey1 [44/0x2C]: %s [%s]", sprint_hex(data, 4), sprint_ascii(data, 4));
    PrintAndLogEx(NORMAL, "         deskey1 [45/0x2D]: %s [%s]", sprint_hex(data + 4, 4), sprint_ascii(data + 4, 4));
    PrintAndLogEx(NORMAL, "         deskey2 [46/0x2E]: %s [%s]", sprint_hex(data + 8, 4), sprint_ascii(data + 8, 4));
    PrintAndLogEx(NORMAL, "         deskey2 [47/0x2F]: %s [%s]", sprint_hex(data + 12, 4), sprint_ascii(data + 12, 4));
    PrintAndLogEx(NORMAL, "\n 3des key: %s", sprint_hex(SwapEndian64(data, 16, 8), 16));
    return PM3_SUCCESS;
}

static int ulc_print_configuration(uint8_t *data) {

    PrintAndLogEx(NORMAL, "\n--- " _CYAN_("UL-C Configuration"));
    PrintAndLogEx(NORMAL, " Higher Lockbits [40/0x28]: %s - %s", sprint_hex(data, 4), sprint_bin(data, 2));
    PrintAndLogEx(NORMAL, "         Counter [41/0x29]: %s - %s", sprint_hex(data + 4, 4), sprint_bin(data + 4, 2));

    bool validAuth = (data[8] >= 0x03 && data[8] <= 0x30);
    if (validAuth)
        PrintAndLogEx(NORMAL, "           Auth0 [42/0x2A]: %s page %d/0x%02X and above need authentication", sprint_hex(data + 8, 4), data[8], data[8]);
    else {
        if (data[8] == 0) {
            PrintAndLogEx(NORMAL, "           Auth0 [42/0x2A]: %s default", sprint_hex(data + 8, 4));
        } else {
            PrintAndLogEx(NORMAL, "           Auth0 [42/0x2A]: %s auth byte is out-of-range", sprint_hex(data + 8, 4));
        }
    }
    PrintAndLogEx(NORMAL, "           Auth1 [43/0x2B]: %s %s",
                  sprint_hex(data + 12, 4),
                  (data[12] & 1) ? "write access restricted" : "read and write access restricted"
                 );
    return PM3_SUCCESS;
}

static int ulev1_print_configuration(uint32_t tagtype, uint8_t *data, uint8_t startPage) {

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Configuration"));

    bool strg_mod_en = (data[0] & 2);

    uint8_t authlim = (data[4] & 0x07);
    bool nfc_cnf_prot_pwd = ((data[4] & 0x08) == 0x08);
    bool nfc_cnf_en  = ((data[4] & 0x10) == 0x10);
    bool cfglck = ((data[4] & 0x40) == 0x40);
    bool prot = ((data[4] & 0x80) == 0x80);

    uint8_t vctid = data[5];

    PrintAndLogEx(INFO, "  cfg0 [%u/0x%02X]: %s", startPage, startPage, sprint_hex(data, 4));

    if ((tagtype & (NTAG_213_F | NTAG_213_TT | NTAG_216_F))) {
        uint8_t mirror_conf = (data[0] & 0xC0);
        uint8_t mirror_byte = (data[0] & 0x30);
        bool sleep_en = (data[0] & 0x08);
        strg_mod_en = (data[0] & 0x04);
        uint8_t fdp_conf = (data[0] & 0x03);

        switch (mirror_conf) {
            case 0:
                PrintAndLogEx(INFO, "                    - no ASCII mirror");
                break;
            case 1:
                PrintAndLogEx(INFO, "                    - UID ASCII mirror");
                break;
            case 2:
                PrintAndLogEx(INFO, "                    - NFC counter ASCII mirror");
                break;
            case 3:
                PrintAndLogEx(INFO, "                    - UID and NFC counter ASCII mirror");
                break;
            default:
                break;
        }

        PrintAndLogEx(INFO, "                    - SLEEP mode %s", (sleep_en) ? "enabled" : "disabled");

        switch (fdp_conf) {
            case 0:
                PrintAndLogEx(INFO, "                    - no field detect");
                break;
            case 1:
                PrintAndLogEx(INFO, "                    - enabled by first State-of-Frame (start of communication)");
                break;
            case 2:
                PrintAndLogEx(INFO, "                    - enabled by selection of the tag");
                break;
            case 3:
                PrintAndLogEx(INFO, "                    - enabled by field presence");
                break;
            default:
                break;
        }
        // valid mirror start page and byte position within start page.
        if ((tagtype & NTAG_213_F) || (tagtype & NTAG_213_TT)) {
            switch (mirror_conf) {
                case 1:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0x24) ? "OK" : "Invalid value"); break;}
                case 2:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0x26) ? "OK" : "Invalid value"); break;}
                case 3:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0x22) ? "OK" : "Invalid value"); break;}
                default:
                    break;
            }
        } else if (tagtype & NTAG_216_F) {
            switch (mirror_conf) {
                case 1:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0xDE) ? "OK" : "Invalid value"); break;}
                case 2:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0xE0) ? "OK" : "Invalid value"); break;}
                case 3:
                { PrintAndLogEx(INFO, "         mirror start block %02X | byte pos %02X - %s", data[2], mirror_byte, (data[2] >= 0x4 && data[2] <= 0xDC) ? "OK" : "Invalid value"); break;}
                default:
                    break;
            }
        }
    }
    PrintAndLogEx(INFO, "                    - strong modulation mode %s", (strg_mod_en) ? "enabled" : "disabled");

    if (data[3] < 0xff)
        PrintAndLogEx(INFO, "                    - page %d and above need authentication", data[3]);
    else
        PrintAndLogEx(INFO, "                    - pages don't need authentication");

    PrintAndLogEx(INFO, "  cfg1 [%u/0x%02X]: %s", startPage + 1, startPage + 1,  sprint_hex(data + 4, 4));
    if (authlim == 0)
        PrintAndLogEx(INFO, "                    - " _GREEN_("Unlimited password attempts"));
    else
        PrintAndLogEx(INFO, "                    - Max number of password attempts is " _YELLOW_("%d"), authlim);

    PrintAndLogEx(INFO, "                    - NFC counter %s", (nfc_cnf_en) ? "enabled" : "disabled");
    PrintAndLogEx(INFO, "                    - NFC counter %s", (nfc_cnf_prot_pwd) ? "password protection enabled" : "not protected");

    PrintAndLogEx(INFO, "                    - user configuration %s", cfglck ? "permanently locked" : "writeable");
    PrintAndLogEx(INFO, "                    - %s access is protected with password", prot ? "read and write" : "write");
    PrintAndLogEx(INFO, "                    - %02X, Virtual Card Type Identifier is %sdefault", vctid, (vctid == 0x05) ? "" : "not ");
    PrintAndLogEx(INFO, "  PWD  [%u/0x%02X]: %s- (cannot be read)", startPage + 2, startPage + 2,  sprint_hex(data + 8, 4));
    PrintAndLogEx(INFO, "  PACK [%u/0x%02X]: %s      - (cannot be read)", startPage + 3, startPage + 3,  sprint_hex(data + 12, 2));
    PrintAndLogEx(INFO, "  RFU  [%u/0x%02X]:       %s- (cannot be read)", startPage + 3, startPage + 3,  sprint_hex(data + 14, 2));
    return PM3_SUCCESS;
}

static int ulev1_print_counters(void) {
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Counters"));
    uint8_t tear[1] = {0};
    uint8_t counter[3] = {0, 0, 0};
    int len = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        ulev1_readTearing(i, tear, sizeof(tear));
        len = ulev1_readCounter(i, counter, sizeof(counter));
        if (len == 3) {
            PrintAndLogEx(INFO, "       [%0d]: %s", i, sprint_hex(counter, 3));
            PrintAndLogEx(SUCCESS, "            - %02X tearing (%s)"
                          , tear[0]
                          , (tear[0] == 0xBD) ? _GREEN_("ok") : _RED_("fail")
                         );
        }
    }
    return len;
}

static int ulev1_print_signature(TagTypeUL_t tagtype, uint8_t *uid, uint8_t *signature, size_t signature_len) {

#define PUBLIC_ECDA_KEYLEN 33
    // known public keys for the originality check (source: https://github.com/alexbatalov/node-nxp-originality-verifier)
    // ref: AN11350 NTAG 21x Originality Signature Validation
    // ref: AN11341 MIFARE Ultralight EV1 Originality Signature Validation
    const ecdsa_publickey_t nxp_mfu_public_keys[] = {
        {"NXP Mifare Classic MFC1C14_x", "044F6D3F294DEA5737F0F46FFEE88A356EED95695DD7E0C27A591E6F6F65962BAF"},
        {"Manufacturer Mifare Classic MFC1C14_x", "046F70AC557F5461CE5052C8E4A7838C11C7A236797E8A0730A101837C004039C2"},
        {"NXP ICODE DNA, ICODE SLIX2", "048878A2A2D3EEC336B4F261A082BD71F9BE11C4E2E896648B32EFA59CEA6E59F0"},
        {"NXP Public key", "04A748B6A632FBEE2C0897702B33BEA1C074998E17B84ACA04FF267E5D2C91F6DC"},
        {"NXP Ultralight Ev1", "0490933BDCD6E99B4E255E3DA55389A827564E11718E017292FAF23226A96614B8"},
        {"NXP NTAG21x (2013)", "04494E1A386D3D3CFE3DC10E5DE68A499B1C202DB5B132393E89ED19FE5BE8BC61"},
        {"MIKRON Public key", "04f971eda742a4a80d32dcf6a814a707cc3dc396d35902f72929fdcd698b3468f2"},
    };

    /*
        uint8_t nxp_mfu_public_keys[6][PUBLIC_ECDA_KEYLEN] = {
            // UL, NTAG21x and NDEF
            {
                0x04, 0x49, 0x4e, 0x1a, 0x38, 0x6d, 0x3d, 0x3c,
                0xfe, 0x3d, 0xc1, 0x0e, 0x5d, 0xe6, 0x8a, 0x49,
                0x9b, 0x1c, 0x20, 0x2d, 0xb5, 0xb1, 0x32, 0x39,
                0x3e, 0x89, 0xed, 0x19, 0xfe, 0x5b, 0xe8, 0xbc, 0x61
            },
            // UL EV1
            {
                0x04, 0x90, 0x93, 0x3b, 0xdc, 0xd6, 0xe9, 0x9b,
                0x4e, 0x25, 0x5e, 0x3d, 0xa5, 0x53, 0x89, 0xa8,
                0x27, 0x56, 0x4e, 0x11, 0x71, 0x8e, 0x01, 0x72,
                0x92, 0xfa, 0xf2, 0x32, 0x26, 0xa9, 0x66, 0x14, 0xb8
            },
            // unknown. Needs identification
            {
                0x04, 0x4F, 0x6D, 0x3F, 0x29, 0x4D, 0xEA, 0x57,
                0x37, 0xF0, 0xF4, 0x6F, 0xFE, 0xE8, 0x8A, 0x35,
                0x6E, 0xED, 0x95, 0x69, 0x5D, 0xD7, 0xE0, 0xC2,
                0x7A, 0x59, 0x1E, 0x6F, 0x6F, 0x65, 0x96, 0x2B, 0xAF
            },
            // unknown. Needs identification
            {
                0x04, 0xA7, 0x48, 0xB6, 0xA6, 0x32, 0xFB, 0xEE,
                0x2C, 0x08, 0x97, 0x70, 0x2B, 0x33, 0xBE, 0xA1,
                0xC0, 0x74, 0x99, 0x8E, 0x17, 0xB8, 0x4A, 0xCA,
                0x04, 0xFF, 0x26, 0x7E, 0x5D, 0x2C, 0x91, 0xF6, 0xDC
            },
            // manufacturer public key
            {
                0x04, 0x6F, 0x70, 0xAC, 0x55, 0x7F, 0x54, 0x61,
                0xCE, 0x50, 0x52, 0xC8, 0xE4, 0xA7, 0x83, 0x8C,
                0x11, 0xC7, 0xA2, 0x36, 0x79, 0x7E, 0x8A, 0x07,
                0x30, 0xA1, 0x01, 0x83, 0x7C, 0x00, 0x40, 0x39, 0xC2
            },
            // MIKRON public key.
            {
                0x04, 0xf9, 0x71, 0xed, 0xa7, 0x42, 0xa4, 0xa8,
                0x0d, 0x32, 0xdc, 0xf6, 0xa8, 0x14, 0xa7, 0x07,
                0xcc, 0x3d, 0xc3, 0x96, 0xd3, 0x59, 0x02, 0xf7,
                0x29, 0x29, 0xfd, 0xcd, 0x69, 0x8b, 0x34, 0x68, 0xf2
            }
        };
    */
    uint8_t i;
    bool is_valid = false;
    for (i = 0; i < ARRAYLEN(nxp_mfu_public_keys); i++) {

        int dl = 0;
        uint8_t key[PUBLIC_ECDA_KEYLEN];
        param_gethex_to_eol(nxp_mfu_public_keys[i].value, 0, key, PUBLIC_ECDA_KEYLEN, &dl);

        int res = ecdsa_signature_r_s_verify(MBEDTLS_ECP_DP_SECP128R1, key, uid, 7, signature, signature_len, false);

        is_valid = (res == 0);
        if (is_valid)
            break;
    }

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Signature"));
    if (is_valid == false || i == ARRAYLEN(nxp_mfu_public_keys)) {
        PrintAndLogEx(INFO, "    Elliptic curve parameters: NID_secp128r1");
        PrintAndLogEx(INFO, "             TAG IC Signature: %s", sprint_hex_inrow(signature, signature_len));
        PrintAndLogEx(SUCCESS, "       Signature verification (" _RED_("fail") ")");
        return PM3_ESOFT;
    }

    PrintAndLogEx(INFO, " IC signature public key name: %s", nxp_mfu_public_keys[i].desc);
    PrintAndLogEx(INFO, "IC signature public key value: %s", nxp_mfu_public_keys[i].value);
    PrintAndLogEx(INFO, "    Elliptic curve parameters: NID_secp128r1");
    PrintAndLogEx(INFO, "             TAG IC Signature: %s", sprint_hex_inrow(signature, signature_len));
    PrintAndLogEx(SUCCESS, "       Signature verification (" _GREEN_("successful") ")");
    return PM3_SUCCESS;
}

static int ulev1_print_version(uint8_t *data) {
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Version"));
    PrintAndLogEx(INFO, "       Raw bytes: %s", sprint_hex(data, 8));
    PrintAndLogEx(INFO, "       Vendor ID: %02X, %s", data[1], getTagInfo(data[1]));
    PrintAndLogEx(INFO, "    Product type: %s", getProductTypeStr(data[2]));
    PrintAndLogEx(INFO, " Product subtype: %02X, %s", data[3], (data[3] == 1) ? "17 pF" : "50pF");
    PrintAndLogEx(INFO, "   Major version: %02X", data[4]);
    PrintAndLogEx(INFO, "   Minor version: %02X", data[5]);
    PrintAndLogEx(INFO, "            Size: %s", getUlev1CardSizeStr(data[6]));
    PrintAndLogEx(INFO, "   Protocol type: %02X%s", data[7], (data[7] == 0x3) ? ", ISO14443-3 Compliant" : "");
    return PM3_SUCCESS;
}

static int ntag_print_counter(void) {
    // NTAG has one counter/tearing.  At address 0x02.
    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Counter"));
    uint8_t tear[1] = {0};
    uint8_t counter[3] = {0, 0, 0};
    uint16_t len;
    len = ulev1_readTearing(0x02, tear, sizeof(tear));
    (void)len;
    len = ulev1_readCounter(0x02, counter, sizeof(counter));
    (void)len;
    PrintAndLogEx(INFO, "       [02]: %s", sprint_hex(counter, 3));
    PrintAndLogEx(SUCCESS, "            - %02X tearing (%s)"
                  , tear[0]
                  , (tear[0] == 0xBD) ? _GREEN_("ok") : _RED_("fail")
                 );
    return len;
}

/*
static int ulc_magic_test(){
    // Magic Ultralight test
        // Magic UL-C, by observation,
    // 1) it seems to have a static nonce response to 0x1A command.
    // 2) the deskey bytes is not-zero:d out on as datasheet states.
    // 3) UID - changeable, not only, but pages 0-1-2-3.
    // 4) use the ul_magic_test !  magic tags answers specially!
    int returnValue = UL_ERROR;
    iso14a_card_select_t card;
    uint8_t nonce1[11] = {0x00};
    uint8_t nonce2[11] = {0x00};
    if ( !ul_select(&card) ){
        return UL_ERROR;
    }
    int status = ulc_requestAuthentication(nonce1, sizeof(nonce1));
    if ( status > 0 ) {
        status = ulc_requestAuthentication(nonce2, sizeof(nonce2));
        returnValue =  ( !memcmp(nonce1, nonce2, 11) ) ? UL_C_MAGIC : UL_C;
    } else {
        returnValue = UL;
    }
    DropField();
    return returnValue;
}
*/
static int ul_magic_test(void) {
    // Magic Ultralight tests
    // 1) take present UID, and try to write it back. OBSOLETE
    // 2) make a wrong length write to page0, and see if tag answers with ACK/NACK:

    iso14a_card_select_t card;
    if (ul_select(&card) == false)
        return UL_ERROR;

    int status = ul_comp_write(0, NULL, 0);
    DropField();
    if (status == 0)
        return MAGIC;

    // check for GEN1A, GEN1B and NTAG21x
    uint8_t is_generation = 0;
    PacketResponseNG resp;
    clearCommandBuffer();
    uint8_t payload[] = { 0 };
    SendCommandNG(CMD_HF_MIFARE_CIDENT, payload, sizeof(payload));
    if (WaitForResponseTimeout(CMD_HF_MIFARE_CIDENT, &resp, 1500)) {
        if (resp.status == PM3_SUCCESS)
            is_generation = resp.data.asBytes[0];
    }
    switch (is_generation) {
        case MAGIC_GEN_1A:
            return MAGIC_1A;
        case MAGIC_GEN_1B:
            return MAGIC_1B;
        case MAGIC_NTAG21X:
            return MAGIC_NTAG;
        default:
            break;
    }
    return 0;
}

uint32_t GetHF14AMfU_Type(void) {

    TagTypeUL_t tagtype = UNKNOWN;
    iso14a_card_select_t card;

    if (ul_select(&card) == false)
        return UL_ERROR;

    // Ultralight - ATQA / SAK
    if (card.atqa[1] != 0x00 || card.atqa[0] != 0x44 || card.sak != 0x00) {
        //PrintAndLogEx(NORMAL, "Tag is not Ultralight | NTAG | MY-D  [ATQA: %02X %02X SAK: %02X]\n", card.atqa[1], card.atqa[0], card.sak);
        DropField();
        return UL_ERROR;
    }

    if (card.uid[0] != 0x05) {

        uint8_t version[10] = {0x00};
        int len  = ulev1_getVersion(version, sizeof(version));
        DropField();

        switch (len) {
            case 0x0A: {
                /*
                MF0UL1001DUx 0004030100000B03
                MF0UL1101DUx 0004030101000B03
                MF0ULH1101DUx 0004030201000B03
                MF0UL1141DUF 0004030301000B03
                MF0UL2101Dxy 0004030101000E03
                MF0UL2101DUx 0004030201000E03
                MF0UL3101DUx 0004030101001103
                MF0ULH3101DUx 0004030201001103
                MF0UL5101DUx 0004030101001303
                NT2L1011F0DUx 0004040101000B03
                NT2H1011G0DUD 0004040201000B03
                NT2L1211F0DUx 0004040101000E03
                NT2H1311G0DUx 0004040201000F03
                NT2H1311F0Dxy 0004040401000F03
                NT2H1411G0DUx 0004040201011103
                NT2H1511G0DUx 0004040201001103
                NT2H1511F0Dxy 0004040401001103
                NT2H1611G0DUx 0004040201001303
                NT2H1611F0Dxy 0004040401001303
                NT2H1311C1DTL 0004040201010F03
                NT2H1311TTDUx 0004040203000F03
                NT3H1101W0FHK 0004040502001303
                NT3H1201W0FHK 0004040502001503
                NT3H1101W0FHK_Variant 0004040502011303
                NT3H1201 0004040502011503
                NT3H2111 0004040502021303
                NT3H2211 0004040502021503
                nhs 0004040600001303
                MF0UN0001DUx 0004030102000B03
                MF0UNH0001DUx 0004030202000B03
                MF0UN1001DUx 0004030103000B03
                MF0UNH1001DUx 0004030203000B03
                NT2L1001G0DUx 0004040102000B03
                NT2H1001G0DUx 0004040202000B03
                NT2H1311TTDUx 0004040203000F03
                Micron UL 0034210101000E03
                */

                if (memcmp(version, "\x00\x04\x03\x01\x01\x00\x0B", 7) == 0)      { tagtype = UL_EV1_48; break; }
                else if (memcmp(version, "\x00\x04\x03\x01\x02\x00\x0B", 7) == 0) { tagtype = UL_NANO_40; break; }
                else if (memcmp(version, "\x00\x04\x03\x02\x01\x00\x0B", 7) == 0) { tagtype = UL_EV1_48; break; }
                else if (memcmp(version, "\x00\x04\x03\x01\x01\x00\x0E", 7) == 0) { tagtype = UL_EV1_128; break; }
                else if (memcmp(version, "\x00\x04\x03\x02\x01\x00\x0E", 7) == 0) { tagtype = UL_EV1_128; break; }
                else if (memcmp(version, "\x00\x34\x21\x01\x01\x00\x0E", 7) == 0) { tagtype = UL_EV1_128; break; } // Mikron JSC Russia EV1 41 pages tag
                else if (memcmp(version, "\x00\x04\x04\x01\x01\x00\x0B", 7) == 0) { tagtype = NTAG_210; break; }
                else if (memcmp(version, "\x00\x04\x04\x01\x01\x00\x0E", 7) == 0) { tagtype = NTAG_212; break; }
                else if (memcmp(version, "\x00\x04\x04\x02\x01\x00\x0F", 7) == 0) { tagtype = NTAG_213; break; }
                else if (memcmp(version, "\x00\x04\x04\x02\x01\x01\x0F", 7) == 0) { tagtype = NTAG_213_C; break; }
                else if (memcmp(version, "\x00\x04\x04\x02\x01\x00\x11", 7) == 0) { tagtype = NTAG_215; break; }
                else if (memcmp(version, "\x00\x04\x04\x02\x01\x00\x13", 7) == 0) { tagtype = NTAG_216; break; }
                else if (memcmp(version, "\x00\x04\x04\x04\x01\x00\x0F", 7) == 0) { tagtype = NTAG_213_F; break; }
                else if (memcmp(version, "\x00\x04\x04\x04\x01\x00\x13", 7) == 0) { tagtype = NTAG_216_F; break; }
                else if (memcmp(version, "\x00\x04\x04\x02\x03\x00\x0F", 7) == 0) { tagtype = NTAG_213_TT; break; }
                else if (memcmp(version, "\x00\x04\x04\x05\x02\x01\x13", 7) == 0) { tagtype = NTAG_I2C_1K; break; }
                else if (memcmp(version, "\x00\x04\x04\x05\x02\x01\x15", 7) == 0) { tagtype = NTAG_I2C_2K; break; }
                else if (memcmp(version, "\x00\x04\x04\x05\x02\x02\x13", 7) == 0) { tagtype = NTAG_I2C_1K_PLUS; break; }
                else if (memcmp(version, "\x00\x04\x04\x05\x02\x02\x15", 7) == 0) { tagtype = NTAG_I2C_2K_PLUS; break; }
                else if (memcmp(version, "\x00\x34\x21\x01\x01\x00\x0E", 7) == 0) { tagtype = UL; break; }
                else if (version[2] == 0x04) { tagtype = NTAG; break; }
                else if (version[2] == 0x03) { tagtype = UL_EV1; }
                break;
            }
            case 0x01:
                tagtype = UL_C;
                break;
            case 0x00:
                tagtype = UL;
                break;
            case -1  :
                tagtype = (UL | UL_C | NTAG_203);
                break;  // could be UL | UL_C magic tags
            default  :
                tagtype = UNKNOWN;
                break;
        }

        // UL vs UL-C vs ntag203 test
        if (tagtype & (UL | UL_C | NTAG_203)) {
            if (!ul_select(&card)) return UL_ERROR;

            // do UL_C check first...
            uint8_t nonce[11] = {0x00};
            int status = ulc_requestAuthentication(nonce, sizeof(nonce));
            DropField();
            if (status > 1) {
                tagtype = UL_C;
            } else {
                // need to re-select after authentication error
                if (ul_select(&card) == false)
                    return UL_ERROR;

                uint8_t data[16] = {0x00};
                // read page 0x26-0x29 (last valid ntag203 page)
                status = ul_read(0x26, data, sizeof(data));
                if (status <= 1) {
                    tagtype = UL;
                } else {
                    // read page 0x30 (should error if it is a ntag203)
                    status = ul_read(0x30, data, sizeof(data));
                    if (status <= 1) {
                        tagtype = NTAG_203;
                    } else {
                        tagtype = UNKNOWN;
                    }
                }
                DropField();
            }
        }
        if (tagtype & UL) {
            tagtype = ul_fudan_check();
            DropField();
        }
    } else {
        DropField();
        // Infinition MY-D tests   Exam high nibble
        uint8_t nib = (card.uid[1] & 0xf0) >> 4;
        switch (nib) {
            // case 0: tagtype =  SLE66R35E7; break; //or SLE 66R35E7 - mifare compat... should have different sak/atqa for mf 1k
            case 1:
                tagtype =  MY_D;
                break; // or SLE 66RxxS ... up to 512 pages of 8 user bytes...
            case 2:
                tagtype = (MY_D_NFC);
                break; // or SLE 66RxxP ... up to 512 pages of 8 user bytes... (or in nfc mode FF pages of 4 bytes)
            case 3:
                tagtype = (MY_D_MOVE | MY_D_MOVE_NFC);
                break; // or SLE 66R01P // 38 pages of 4 bytes //notice: we can not currently distinguish between these two
            case 7:
                tagtype =  MY_D_MOVE_LEAN;
                break; // or SLE 66R01L  // 16 pages of 4 bytes
        }
    }

    tagtype |= ul_magic_test();
    if (tagtype == (UNKNOWN | MAGIC)) {
        tagtype = (UL_MAGIC);
    }
    return tagtype;
}
//
//  extended tag information
//
static int CmdHF14AMfUInfo(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu info",
                  "Get info about MIFARE Ultralight Family styled tag.\n"
                  "Sometimes the tags are locked down, and you may need a key to be able to read the information",
                  "hf mfu info\n"
                  "hf mfu info -k AABBCCDD\n"
                  "hf mfu info --key 00112233445566778899AABBCCDDEEFF"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("k", "key", "<hex>", "key for authentication (UL-C 16 bytes, EV1/NTAG 4 bytes)"),
        arg_lit0("l", NULL,           "swap entered key's endianness"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    int ak_len = 0;
    uint8_t authenticationkey[16] = {0x00};
    CLIGetHexWithReturn(ctx, 1, authenticationkey, &ak_len);
    bool swap_endian = arg_get_lit(ctx, 2);
    CLIParserFree(ctx);

    if (ak_len) {
        if (ak_len != 16 && ak_len != 8) {
            PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
            return PM3_EINVARG;
        }
    }

    bool has_auth_key = false;
    if (ak_len > 0)
        has_auth_key = true;

    uint8_t authlim = 0xff;
    uint8_t data[16] = {0x00};
    iso14a_card_select_t card;
    int status;
    uint8_t *authkeyptr = authenticationkey;
    uint8_t pwd[4] = {0, 0, 0, 0};
    uint8_t *key = pwd;
    uint8_t pack[4] = {0, 0, 0, 0};
    int len;

    TagTypeUL_t tagtype = GetHF14AMfU_Type();
    if (tagtype == UL_ERROR)
        return PM3_ESOFT;

    PrintAndLogEx(NORMAL, "");
    PrintAndLogEx(INFO, "--- " _CYAN_("Tag Information") " --------------------------");
    PrintAndLogEx(INFO, "-------------------------------------------------------------");
    ul_print_type(tagtype, 6);

    // Swap endianness
    if (swap_endian && has_auth_key) {
        authkeyptr = SwapEndian64(authenticationkey, ak_len, (ak_len == 16) ? 8 : 4);
    }

    if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
        return PM3_ESOFT;
    }

    bool locked = false;
    // read pages 0,1,2,3 (should read 4pages)
    status = ul_read(0, data, sizeof(data));
    if (status == -1) {
        DropField();
        PrintAndLogEx(ERR, "Error: tag didn't answer to READ");
        return PM3_ESOFT;
    } else if (status == 16) {
        ul_print_default(data, card.uid);
        ndef_print_CC(data + 12);
    } else {
        locked = true;
    }

    // UL_C Specific
    if ((tagtype & UL_C)) {

        // read pages 0x28, 0x29, 0x2A, 0x2B
        uint8_t ulc_conf[16] = {0x00};
        status = ul_read(0x28, ulc_conf, sizeof(ulc_conf));
        if (status == -1) {
            PrintAndLogEx(ERR, "Error: tag didn't answer to READ UL-C");
            DropField();
            return PM3_ESOFT;
        }
        if (status == 16)
            ulc_print_configuration(ulc_conf);
        else
            locked = true;

        if ((tagtype & MAGIC)) {
            //just read key
            uint8_t ulc_deskey[16] = {0x00};
            status = ul_read(0x2C, ulc_deskey, sizeof(ulc_deskey));
            if (status == -1) {
                DropField();
                PrintAndLogEx(ERR, "Error: tag didn't answer to READ magic");
                return PM3_ESOFT;
            }
            if (status == 16) {
                ulc_print_3deskey(ulc_deskey);
            }

        } else {
            DropField();
            // if we called info with key, just return
            if (has_auth_key) {
                return PM3_SUCCESS;
            }

            // also try to diversify default keys..  look into CmdHF14AMfGenDiverseKeys
            if (try_default_3des_keys(&key)) {
                PrintAndLogEx(SUCCESS, "Found default 3des key: ");
                uint8_t keySwap[16];
                memcpy(keySwap, SwapEndian64(key, 16, 8), 16);
                ulc_print_3deskey(keySwap);
            }
            return PM3_SUCCESS;
        }
    }

    // do counters and signature first (don't neet auth)

    // ul counters are different than ntag counters
    if ((tagtype & (UL_EV1_48 | UL_EV1_128 | UL_EV1))) {
        if (ulev1_print_counters() != 3) {
            // failed - re-select
            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }
        }
    }

    // NTAG counters?
    if ((tagtype & (NTAG_213 | NTAG_213_F | NTAG_213_C | NTAG_213_TT | NTAG_215 | NTAG_216))) {
        if (ntag_print_counter()) {
            // failed - re-select
            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }
        }
    }

    // Read signature
    if ((tagtype & (UL_EV1_48 | UL_EV1_128 | UL_EV1 | UL_NANO_40 | NTAG_213 | NTAG_213_F | NTAG_213_C | NTAG_213_TT | NTAG_215 | NTAG_216 | NTAG_216_F | NTAG_I2C_1K | NTAG_I2C_2K | NTAG_I2C_1K_PLUS | NTAG_I2C_2K_PLUS))) {
        uint8_t ulev1_signature[32] = {0x00};
        status = ulev1_readSignature(ulev1_signature, sizeof(ulev1_signature));
        if (status == -1) {
            PrintAndLogEx(ERR, "Error: tag didn't answer to READ SIGNATURE");
            DropField();
            return PM3_ESOFT;
        }
        if (status == 32) {
            ulev1_print_signature(tagtype, card.uid, ulev1_signature, sizeof(ulev1_signature));
        } else {
            // re-select
            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }
        }

        // Get Version
        uint8_t version[10] = {0x00};
        status  = ulev1_getVersion(version, sizeof(version));
        if (status == -1) {
            PrintAndLogEx(ERR, "Error: tag didn't answer to GETVERSION");
            DropField();
            return PM3_ESOFT;
        } else if (status == 10) {
            ulev1_print_version(version);
        } else {
            locked = true;
            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }
        }

        uint8_t startconfigblock = 0;
        uint8_t ulev1_conf[16] = {0x00};

        // config blocks always are last 4 pages
        for (uint8_t i = 0; i < ARRAYLEN(UL_TYPES_ARRAY); i++) {
            if (tagtype & UL_TYPES_ARRAY[i]) {
                startconfigblock = UL_MEMORY_ARRAY[i] - 3;
                break;
            }
        }

        if (startconfigblock) { // if we know where the config block is...
            status = ul_read(startconfigblock, ulev1_conf, sizeof(ulev1_conf));
            if (status == -1) {
                PrintAndLogEx(ERR, "Error: tag didn't answer to READ EV1");
                DropField();
                return PM3_ESOFT;
            } else if (status == 16) {
                // save AUTHENTICATION LIMITS for later:
                authlim = (ulev1_conf[4] & 0x07);
                // add pwd / pack if used from cli
                if (has_auth_key) {
                    memcpy(ulev1_conf + 8, authkeyptr, 4);
                    memcpy(ulev1_conf + 12, pack, 2);
                }
                ulev1_print_configuration(tagtype, ulev1_conf, startconfigblock);
            }
        }

        // AUTHLIMIT, (number of failed authentications)
        // 0 = limitless.
        // 1-7 = limit. No automatic tries then.
        // hasAuthKey,  if we was called with key, skip test.
        if (!authlim && !has_auth_key) {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(SUCCESS, "--- " _CYAN_("Known EV1/NTAG passwords"));
            // test pwd gen A
            num_to_bytes(ul_ev1_pwdgenA(card.uid), 4, key);
            len = ulev1_requestAuthentication(key, pack, sizeof(pack));
            if (len > -1) {
                PrintAndLogEx(SUCCESS, "Found a default password: " _GREEN_("%s") " Pack: %02X %02X", sprint_hex(key, 4), pack[0], pack[1]);
                goto out;
            }

            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }

            // test pwd gen B
            num_to_bytes(ul_ev1_pwdgenB(card.uid), 4, key);
            len = ulev1_requestAuthentication(key, pack, sizeof(pack));
            if (len > -1) {
                PrintAndLogEx(SUCCESS, "Found a default password: " _GREEN_("%s") " Pack: %02X %02X", sprint_hex(key, 4), pack[0], pack[1]);
                goto out;
            }

            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }

            // test pwd gen C
            num_to_bytes(ul_ev1_pwdgenC(card.uid), 4, key);
            len = ulev1_requestAuthentication(key, pack, sizeof(pack));
            if (len > -1) {
                PrintAndLogEx(SUCCESS, "Found a default password: " _GREEN_("%s") " Pack: %02X %02X", sprint_hex(key, 4), pack[0], pack[1]);
                goto out;
            }

            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }

            // test pwd gen D
            num_to_bytes(ul_ev1_pwdgenD(card.uid), 4, key);
            len = ulev1_requestAuthentication(key, pack, sizeof(pack));
            if (len > -1) {
                PrintAndLogEx(SUCCESS, "Found a default password:" _GREEN_("%s") " Pack: %02X %02X", sprint_hex(key, 4), pack[0], pack[1]);
                goto out;
            }

            if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                return PM3_ESOFT;
            }

            for (uint8_t i = 0; i < ARRAYLEN(default_pwd_pack); ++i) {
                key = default_pwd_pack[i];
                len = ulev1_requestAuthentication(key, pack, sizeof(pack));
                if (len > -1) {
                    PrintAndLogEx(SUCCESS, "Found a default password: " _GREEN_("%s") " Pack: %02X %02X", sprint_hex(key, 4), pack[0], pack[1]);
                    break;
                } else {
                    if (ul_auth_select(&card, tagtype, has_auth_key, authkeyptr, pack, sizeof(pack)) == PM3_ESOFT) {
                        return PM3_ESOFT;
                    }
                }
            }
            if (len < 1) {
                PrintAndLogEx(WARNING, _YELLOW_("password not known"));
                PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfu pwdgen r`") " to get see known pwd gen algo suggestions");
            }
        } else {
            PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfu pwdgen r`") " to get see known pwd gen algo suggestions");
        }
    }
out:
    DropField();
    if (locked) {
        PrintAndLogEx(INFO, "\nTag appears to be locked, try using a key to get more info");
        PrintAndLogEx(HINT, "Hint: try " _YELLOW_("`hf mfu pwdgen r`") " to get see known pwd gen algo suggestions");
    }
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

//
//  Write Single Block
//
static int CmdHF14AMfUWrBl(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu wrbl",
                  "Write a block. It autodetects card type.",
                  "hf mfu wrbl -b 0 -d 01234567\n"
                  "hf mfu wrbl -b 0 -d 01234567 -k AABBCCDD\n"
                  "hf mfu wrbl -b 0 -d 01234567 -k 00112233445566778899AABBCCDDEEFF"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("k", "key", "<hex>", "key for authentication (UL-C 16 bytes, EV1/NTAG 4 bytes)"),
        arg_lit0("l", NULL,           "swap entered key's endianness"),
        arg_int1("b", "block", "<dec>", "block number to write"),
        arg_str1("d", "data", "<hex>", "block data (4 or 16 hex bytes, 16 hex bytes will do a compatibility write)"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    int ak_len = 0;
    uint8_t authenticationkey[16] = {0x00};
    CLIGetHexWithReturn(ctx, 1, authenticationkey, &ak_len);
    bool swap_endian = arg_get_lit(ctx, 2);

    int blockno = arg_get_int_def(ctx, 3, -1);

    int datalen = 0;
    uint8_t data[16] = {0x00};
    CLIGetHexWithReturn(ctx, 4, data, &datalen);
    CLIParserFree(ctx);

    bool has_auth_key = false;
    bool has_pwd = false;
    if (ak_len == 16) {
        has_auth_key = true;
    } else if (ak_len == 4) {
        has_pwd = true;
    } else if (ak_len != 0) {
        PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
        return PM3_EINVARG;
    }

    if (blockno < 0) {
        PrintAndLogEx(WARNING, "Wrong block number");
        return PM3_EINVARG;
    }

    if (datalen != 16 && datalen != 4) {
        PrintAndLogEx(WARNING, "Wrong data length. Expect 16 or 4, got %d", datalen);
        return PM3_EINVARG;
    }

    uint8_t *authKeyPtr = authenticationkey;

    // starting with getting tagtype
    TagTypeUL_t tagtype = GetHF14AMfU_Type();
    if (tagtype == UL_ERROR)
        return PM3_ESOFT;

    uint8_t maxblockno = 0;
    for (uint8_t idx = 0; idx < ARRAYLEN(UL_TYPES_ARRAY); idx++) {
        if (tagtype & UL_TYPES_ARRAY[idx]) {
            maxblockno = UL_MEMORY_ARRAY[idx];
            break;
        }
    }
    if (blockno > maxblockno) {
        PrintAndLogEx(WARNING, "block number too large. Max block is %u/0x%02X \n", maxblockno, maxblockno);
        return PM3_EINVARG;
    }

    // Swap endianness
    if (swap_endian) {
        if (has_auth_key)
            authKeyPtr = SwapEndian64(authenticationkey, 16, 8);

        if (has_pwd)
            authKeyPtr = SwapEndian64(authenticationkey, 4, 4);
    }

    if (blockno <= 3)
        PrintAndLogEx(INFO, "Special block: %0d (0x%02X) [ %s]", blockno, blockno, sprint_hex(data, datalen));
    else
        PrintAndLogEx(INFO, "Block: %0d (0x%02X) [ %s]", blockno, blockno, sprint_hex(data, datalen));

    if (ak_len) {
        PrintAndLogEx(INFO, "Using %s " _GREEN_("%s"), (ak_len == 16) ? "3des" : "pwd", sprint_hex(authenticationkey, ak_len));
    }

    //Send write Block

    // 4 or 16.
    uint8_t cmddata[32];
    memcpy(cmddata, data, datalen);

    // 0 - no pwd/key, no authentication
    // 1 - 3des key (16 bytes)
    // 2 - pwd  (4 bytes)
    uint8_t keytype = 0;
    size_t cmdlen = datalen;
    if (has_auth_key) {
        keytype = 1;
        memcpy(cmddata + datalen, authKeyPtr, 16);
        cmdlen += 16;
    } else if (has_pwd) {
        keytype = 2;
        memcpy(cmddata + datalen, authKeyPtr, 4);
        cmdlen += 4;
    }

    clearCommandBuffer();
    if (datalen == 16) {
        SendCommandMIX(CMD_HF_MIFAREU_WRITEBL_COMPAT, blockno, keytype, 0, cmddata, cmdlen);
    } else {
        SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, blockno, keytype, 0, cmddata, cmdlen);
    }
    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        uint8_t isOK  = resp.oldarg[0] & 0xff;
        PrintAndLogEx(SUCCESS, "isOk:%02x", isOK);
    } else {
        PrintAndLogEx(WARNING, "Command execute timeout");
    }

    return PM3_SUCCESS;
}
//
//  Read Single Block
//
static int CmdHF14AMfURdBl(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu rdbl",
                  "Read a block and print. It autodetects card type.",
                  "hf mfu rdbl -b 0\n"
                  "hf mfu rdbl -b 0 -k AABBCCDD\n"
                  "hf mfu rdbl -b 0 --key 00112233445566778899AABBCCDDEEFF"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("k", "key", "<hex>", "key for authentication (UL-C 16 bytes, EV1/NTAG 4 bytes)"),
        arg_lit0("l", NULL,           "swap entered key's endianness"),
        arg_int1("b", "block", "<dec>", "block number to write"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    int ak_len = 0;
    uint8_t authenticationkey[16] = {0x00};
    CLIGetHexWithReturn(ctx, 1, authenticationkey, &ak_len);
    bool swap_endian = arg_get_lit(ctx, 2);
    int blockno = arg_get_int_def(ctx, 3, -1);
    CLIParserFree(ctx);

    bool has_auth_key = false;
    bool has_pwd = false;
    if (ak_len == 16) {
        has_auth_key = true;
    } else if (ak_len == 4) {
        has_pwd = true;
    } else if (ak_len != 0) {
        PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
        return PM3_EINVARG;
    }

    if (blockno < 0) {
        PrintAndLogEx(WARNING, "Wrong block number");
        return PM3_EINVARG;
    }

    uint8_t *authKeyPtr = authenticationkey;

    // start with getting tagtype
    TagTypeUL_t tagtype = GetHF14AMfU_Type();
    if (tagtype == UL_ERROR)
        return PM3_ESOFT;

    uint8_t maxblockno = 0;
    for (uint8_t idx = 0; idx < ARRAYLEN(UL_TYPES_ARRAY); idx++) {
        if (tagtype & UL_TYPES_ARRAY[idx]) {
            maxblockno = UL_MEMORY_ARRAY[idx];
            break;
        }
    }
    if (blockno > maxblockno) {
        PrintAndLogEx(WARNING, "block number to large. Max block is %u/0x%02X \n", maxblockno, maxblockno);
        return PM3_EINVARG;
    }

    // Swap endianness
    if (swap_endian) {
        if (has_auth_key)
            authKeyPtr = SwapEndian64(authenticationkey, ak_len, 8);

        if (has_pwd)
            authKeyPtr = SwapEndian64(authenticationkey, ak_len, 4);
    }

    if (ak_len) {
        PrintAndLogEx(INFO, "Using %s " _GREEN_("%s"), (ak_len == 16) ? "3des" : "pwd", sprint_hex(authenticationkey, ak_len));
    }

    //Read Block
    uint8_t keytype = 0;
    uint8_t datalen = 0;
    if (has_auth_key) {
        keytype = 1;
        datalen = 16;
    } else if (has_pwd) {
        keytype = 2;
        datalen = 4;
    }

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_READBL, blockno, keytype, 0, authKeyPtr, datalen);
    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        uint8_t isOK = resp.oldarg[0] & 0xff;
        if (isOK) {
            uint8_t *d = resp.data.asBytes;
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(INFO, "Block#  | Data        | Ascii");
            PrintAndLogEx(INFO, "-----------------------------");
            PrintAndLogEx(INFO, "%02d/0x%02X | %s| %s\n", blockno, blockno, sprint_hex(d, 4), sprint_ascii(d, 4));
        } else {
            PrintAndLogEx(WARNING, "Failed reading block: (%02x)", isOK);
        }
    } else {
        PrintAndLogEx(WARNING, "Command execute time-out");
    }
    return PM3_SUCCESS;
}

void printMFUdumpEx(mfu_dump_t *card, uint16_t pages, uint8_t startpage) {

    PrintAndLogEx(INFO, _CYAN_("MFU dump file information"));
    PrintAndLogEx(INFO, "-------------------------------------------------------------");
    PrintAndLogEx(INFO, "      Version | " _YELLOW_("%s"), sprint_hex(card->version, sizeof(card->version)));
    PrintAndLogEx(INFO, "        TBD 0 | %s", sprint_hex(card->tbo, sizeof(card->tbo)));
    PrintAndLogEx(INFO, "        TBD 1 | %s", sprint_hex(card->tbo1, sizeof(card->tbo1)));
    PrintAndLogEx(INFO, "    Signature | %s", sprint_hex(card->signature, sizeof(card->signature)));
    for (uint8_t i = 0; i < 3; i ++) {
        PrintAndLogEx(INFO, "    Counter %d | %s", i, sprint_hex(card->counter_tearing[i], 3));
        PrintAndLogEx(INFO, "    Tearing %d | %s", i, sprint_hex(card->counter_tearing[i] + 3, 1));
    }

    PrintAndLogEx(INFO, "Max data page | " _YELLOW_("%d") " (" _YELLOW_("%d") " bytes)", card->pages - 1, card->pages * 4);
    PrintAndLogEx(INFO, "  Header size | %d", MFU_DUMP_PREFIX_LENGTH);
    PrintAndLogEx(INFO, "-------------------------------------------------------------");
    PrintAndLogEx(INFO, "block#   | data        |lck| ascii");
    PrintAndLogEx(INFO, "---------+-------------+---+------");

    uint8_t j = 0;
    bool lckbit = false;
    uint8_t *data = card->data;

    uint8_t lockbytes_sta[] = {0, 0};
    uint8_t lockbytes_dyn[] = {0, 0, 0};
    bool bit_stat[16]  = {0};
    bool bit_dyn[16] = {0};

    // Load static lock bytes.
    memcpy(lockbytes_sta, data + 10, sizeof(lockbytes_sta));
    for (j = 0; j < 16; j++) {
        bit_stat[j] = lockbytes_sta[j / 8] & (1 << (7 - j % 8));
    }

    // Load dynamic lockbytes if available
    // TODO -- FIGURE OUT LOCK BYTES FOR TO EV1 and/or NTAG
    if (pages == 44) {

        memcpy(lockbytes_dyn, data + (40 * 4), sizeof(lockbytes_dyn));

        for (j = 0; j < 16; j++) {
            bit_dyn[j] = lockbytes_dyn[j / 8] & (1 << (7 - j % 8));
        }
        PrintAndLogEx(INFO, "DYNAMIC LOCK: %s", sprint_hex(lockbytes_dyn, 3));
    }

    for (uint8_t i = 0; i < pages; ++i) {
        if (i < 3) {
            PrintAndLogEx(INFO, "%3d/0x%02X | %s|   | %s", i + startpage, i + startpage, sprint_hex(data + i * 4, 4), sprint_ascii(data + i * 4, 4));
            continue;
        }
        switch (i) {
            case  3:
                lckbit = bit_stat[4];
                break;
            case  4:
                lckbit = bit_stat[3];
                break;
            case  5:
                lckbit = bit_stat[2];
                break;
            case  6:
                lckbit = bit_stat[1];
                break;
            case  7:
                lckbit = bit_stat[0];
                break;
            case  8:
                lckbit = bit_stat[15];
                break;
            case  9:
                lckbit = bit_stat[14];
                break;
            case 10:
                lckbit = bit_stat[13];
                break;
            case 11:
                lckbit = bit_stat[12];
                break;
            case 12:
                lckbit = bit_stat[11];
                break;
            case 13:
                lckbit = bit_stat[10];
                break;
            case 14:
                lckbit = bit_stat[9];
                break;
            case 15:
                lckbit = bit_stat[8];
                break;
            case 16:
            case 17:
            case 18:
            case 19:
                lckbit = bit_dyn[6];
                break;
            case 20:
            case 21:
            case 22:
            case 23:
                lckbit = bit_dyn[5];
                break;
            case 24:
            case 25:
            case 26:
            case 27:
                lckbit = bit_dyn[4];
                break;
            case 28:
            case 29:
            case 30:
            case 31:
                lckbit = bit_dyn[2];
                break;
            case 32:
            case 33:
            case 34:
            case 35:
                lckbit = bit_dyn[1];
                break;
            case 36:
            case 37:
            case 38:
            case 39:
                lckbit = bit_dyn[0];
                break;
            case 40:
                lckbit = bit_dyn[12];
                break;
            case 41:
                lckbit = bit_dyn[11];
                break;
            case 42:
                lckbit = bit_dyn[10];
                break; //auth0
            case 43:
                lckbit = bit_dyn[9];
                break;  //auth1
            default:
                break;
        }
        PrintAndLogEx(INFO, "%3d/0x%02X | %s| %s | %s", i + startpage, i + startpage, sprint_hex(data + i * 4, 4), (lckbit) ? _RED_("1") : "0", sprint_ascii(data + i * 4, 4));
    }
    PrintAndLogEx(INFO, "---------------------------------");
}

//
//  Mifare Ultralight / Ultralight-C / Ultralight-EV1
//  Read and Dump Card Contents,  using auto detection of tag size.
static int CmdHF14AMfUDump(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu dump",
                  "Reads all pages from Ultralight, Ultralight-C, Ultralight EV1\n"
                  "NTAG 203, NTAG 210, NTAG 212, NTAG 213, NTAG 215, NTAG 216\n"
                  "and saves data into binary/json files.\n"
                  "It autodetects card type.",
                  "hf mfu dump -f myfile        -> dump whole tag, save to `myfile.bin`\n"
                  "hf mfu dump -k AABBCCDD      -> dump whole tag using pwd AABBCCDD\n"
                  "hf mfu dump -p 10            -> start at page 10 and dump rest of blocks\n"
                  "hf mfu dump -p 10 -q 2       -> start at page 10 and dump two blocks\n"
                  "hf mfu dump --key 00112233445566778899AABBCCDDEEFF"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_str0("f", "file", "<fn>", "specify a filename for dump file"),
        arg_str0("k", "key", "<hex>", "key for authentication (UL-C 16 bytes, EV1/NTAG 4 bytes)"),
        arg_lit0("l", NULL,           "swap entered key's endianness"),
        arg_int0("p", "page", "<dec>", "manually set start page number to start from"),
        arg_int0("q", "qty", "<dec>", "manually set number of pages to dump"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);

    int fnlen = 0;
    char filename[FILE_PATH_SIZE] = {0};
    CLIParamStrToBuf(arg_get_str(ctx, 1), (uint8_t *)filename, FILE_PATH_SIZE, &fnlen);

    int ak_len = 0;
    uint8_t authenticationkey[16] = {0x00};
    uint8_t *authKeyPtr = authenticationkey;
    CLIGetHexWithReturn(ctx, 2, authenticationkey, &ak_len);
    bool swap_endian = arg_get_lit(ctx, 3);
    int start_page = arg_get_int_def(ctx, 4, 0);
    int pages = arg_get_int_def(ctx, 5, 16);
    CLIParserFree(ctx);

    bool has_auth_key = false;
    bool has_pwd = false;
    if (ak_len == 16) {
        has_auth_key = true;
    } else if (ak_len == 4) {
        has_pwd = true;
    } else if (ak_len != 0) {
        PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
        return PM3_EINVARG;
    }

    bool manual_pages = false;
    if (start_page > 0)
        manual_pages = true;

    if (pages != 16)
        manual_pages = true;

    uint8_t card_mem_size = 0;

    // Swap endianness
    if (swap_endian) {
        if (has_auth_key)
            authKeyPtr = SwapEndian64(authenticationkey, ak_len, 8);

        if (has_pwd)
            authKeyPtr = SwapEndian64(authenticationkey, ak_len, 4);
    }

    TagTypeUL_t tagtype = GetHF14AMfU_Type();
    if (tagtype == UL_ERROR)
        return PM3_ESOFT;

    //get number of pages to read
    if (manual_pages == false) {
        for (uint8_t idx = 0; idx < ARRAYLEN(UL_TYPES_ARRAY); idx++) {
            if (tagtype & UL_TYPES_ARRAY[idx]) {
                //add one as maxblks starts at 0
                card_mem_size = pages = UL_MEMORY_ARRAY[idx] + 1;
                break;
            }
        }
    }
    ul_print_type(tagtype, 0);
    PrintAndLogEx(SUCCESS, "Reading tag memory...");
    uint8_t keytype = 0;
    if (has_auth_key) {
        if (tagtype & UL_C)
            keytype = 1; //UL_C auth
        else
            keytype = 2; //UL_EV1/NTAG auth
    }

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_READCARD, start_page, pages, keytype, authKeyPtr, ak_len);

    PacketResponseNG resp;
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 2500)) {
        PrintAndLogEx(WARNING, "Command execute time-out");
        return PM3_ETIMEOUT;
    }

    if (resp.oldarg[0] != 1) {
        PrintAndLogEx(WARNING, "Failed dumping card");
        return PM3_ESOFT;
    }

    // read all memory
    uint8_t data[1024] = {0x00};
    memset(data, 0x00, sizeof(data));

    uint32_t startindex = resp.oldarg[2];
    uint32_t buffer_size = resp.oldarg[1];
    if (buffer_size > sizeof(data)) {
        PrintAndLogEx(FAILED, "Data exceeded Buffer size!");
        buffer_size = sizeof(data);
    }

    if (!GetFromDevice(BIG_BUF, data, buffer_size, startindex, NULL, 0, NULL, 2500, false)) {
        PrintAndLogEx(WARNING, "command execution time out");
        return PM3_ETIMEOUT;
    }

    bool is_partial = (pages != buffer_size / 4);

    pages = buffer_size / 4;

    iso14a_card_select_t card;
    mfu_dump_t dump_file_data;
    memset(&dump_file_data, 0, sizeof(dump_file_data));
    uint8_t get_version[] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t get_counter_tearing[][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    uint8_t get_signature[32];
    memset(get_signature, 0, sizeof(get_signature));

    // not ul_c and not std ul then attempt to collect info like
    //  VERSION, SIGNATURE, COUNTERS, TEARING, PACK,
    if (!(tagtype & UL_C || tagtype & UL || tagtype & MY_D_MOVE || tagtype & MY_D_MOVE_LEAN)) {
        //attempt to read pack
        uint8_t get_pack[] = {0, 0};
        if (ul_auth_select(&card, tagtype, true, authKeyPtr, get_pack, sizeof(get_pack)) != PM3_SUCCESS) {
            //reset pack
            get_pack[0] = 0;
            get_pack[1] = 0;
        }
        DropField();

        // only add pack if not partial read,  and complete pages read.
        if (!is_partial && pages == card_mem_size) {

            // add pack to block read
            memcpy(data + (pages * 4) - 4, get_pack, sizeof(get_pack));
        }

        if (has_auth_key) {
            uint8_t dummy_pack[] = {0, 0};
            ul_auth_select(&card, tagtype, has_auth_key, authKeyPtr, dummy_pack, sizeof(dummy_pack));
        } else {
            ul_select(&card);
        }

        ulev1_getVersion(get_version, sizeof(get_version));

        // ULEV-1 has 3 counters
        uint8_t n = 0;

        // NTAG has 1 counter, at 0x02
        if ((tagtype & (NTAG_213 | NTAG_213_F | NTAG_213_C | NTAG_213_TT | NTAG_215 | NTAG_216))) {
            n = 2;
        }

        // NTAG can have nfc counter pwd protection enabled
        for (; n < 3; n++) {

            if (has_auth_key) {
                uint8_t dummy_pack[] = {0, 0};
                ul_auth_select(&card, tagtype, has_auth_key, authKeyPtr, dummy_pack, sizeof(dummy_pack));
            } else {
                ul_select(&card);
            }
            ulev1_readCounter(n, &get_counter_tearing[n][0], 3);

            if (has_auth_key) {
                uint8_t dummy_pack[] = {0, 0};
                ul_auth_select(&card, tagtype, has_auth_key, authKeyPtr, dummy_pack, sizeof(dummy_pack));
            } else {
                ul_select(&card);
            }
            ulev1_readTearing(n, &get_counter_tearing[n][3], 1);
        }

        DropField();

        if (has_auth_key) {
            uint8_t dummy_pack[] = {0, 0};
            ul_auth_select(&card, tagtype, has_auth_key, authKeyPtr, dummy_pack, sizeof(dummy_pack));
        } else
            ul_select(&card);

        ulev1_readSignature(get_signature, sizeof(get_signature));
        DropField();
    }

    // format and add keys to block dump output
    // only add keys if not partial read, and complete pages read
    if (!is_partial && pages == card_mem_size && has_auth_key) {
        // if we didn't swapendian before - do it now for the sprint_hex call
        // NOTE: default entry is bigendian (unless swapped), sprint_hex outputs little endian
        //       need to swap to keep it the same
        if (swap_endian == false) {
            authKeyPtr = SwapEndian64(authenticationkey, ak_len, (ak_len == 16) ? 8 : 4);
        } else {
            authKeyPtr = authenticationkey;
        }

        if (tagtype & UL_C) { //add 4 pages
            memcpy(data + pages * 4, authKeyPtr, ak_len);
            pages += ak_len / 4;
        } else { // 2nd page from end
            memcpy(data + (pages * 4) - 8, authenticationkey, ak_len);
        }
    }

    //add *special* blocks to dump
    // pack and pwd saved into last pages of dump, if was not partial read
    dump_file_data.pages = pages - 1;
    memcpy(dump_file_data.version, get_version, sizeof(dump_file_data.version));
    memcpy(dump_file_data.signature, get_signature, sizeof(dump_file_data.signature));
    memcpy(dump_file_data.counter_tearing, get_counter_tearing, sizeof(dump_file_data.counter_tearing));
    memcpy(dump_file_data.data, data, pages * 4);

    printMFUdumpEx(&dump_file_data, pages, start_page);

    // user supplied filename?
    if (fnlen < 1) {

        PrintAndLogEx(INFO, "Using UID as filename");
        uint8_t uid[7] = {0};
        memcpy(uid, (uint8_t *)&dump_file_data.data, 3);
        memcpy(uid + 3, (uint8_t *)&dump_file_data.data + 4, 4);
        strcat(filename, "hf-mfu-");
        FillFileNameByUID(filename, uid, "-dump", sizeof(uid));
    }
    uint16_t datalen = pages * 4 + MFU_DUMP_PREFIX_LENGTH;
    saveFile(filename, ".bin", (uint8_t *)&dump_file_data, datalen);
    saveFileJSON(filename, jsfMfuMemory, (uint8_t *)&dump_file_data, datalen, NULL);

    if (is_partial)
        PrintAndLogEx(WARNING, "Partial dump created. (%d of %d blocks)", pages, card_mem_size);

    return PM3_SUCCESS;
}

static void wait4response(uint8_t b) {
    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        uint8_t isOK  = resp.oldarg[0] & 0xff;
        if (!isOK)
            PrintAndLogEx(WARNING, "failed to write block %d", b);
    } else {
        PrintAndLogEx(WARNING, "Command execute timeout");
    }
}

//
//  Restore dump file onto tag
//
static int CmdHF14AMfURestore(const char *Cmd) {

    char tempStr[50] = {0};
    char filename[FILE_PATH_SIZE] = {0};
    uint8_t authkey[16] = {0};
    uint8_t *p_authkey = authkey;
    uint8_t cmdp = 0, keylen = 0;
    bool hasKey = false;
    bool swapEndian = false;
    bool errors = false;
    bool write_special = false;
    bool write_extra = false;
    bool read_key = false;
    bool verbose = false;
    size_t filelen = 0;

    memset(authkey, 0x00, sizeof(authkey));

    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (tolower(param_getchar(Cmd, cmdp))) {
            case 'h':
                return usage_hf_mfu_restore();
            case 'k':
                keylen = param_getstr(Cmd, cmdp + 1, tempStr, sizeof(tempStr));
                if (keylen == 32 || keylen == 8) { //ul-c or ev1/ntag key length
                    errors = param_gethex(tempStr, 0, authkey, keylen);
                    keylen /= 2;
                } else {
                    PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
                    errors = true;
                }
                cmdp += 2;
                hasKey = true;
                break;
            case 'l':
                swapEndian = true;
                cmdp++;
                break;
            case 'f':
                filelen = param_getstr(Cmd, cmdp + 1, filename, FILE_PATH_SIZE);

                if (filelen > FILE_PATH_SIZE - 5)
                    filelen = FILE_PATH_SIZE - 5;

                if (filelen < 1)
                    sprintf(filename, "dumpdata.bin");

                cmdp += 2;
                break;
            case 's':
                cmdp++;
                write_special = true;
                break;
            case 'e':
                cmdp++;
                write_extra = true;
                break;
            case 'r':
                cmdp++;
                read_key = true;
                break;
            case 'v':
                cmdp++;
                verbose = true;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter: " _RED_("'%c'"), param_getchar(Cmd, cmdp));
                errors = true;
                break;
        }
    }

    //Validations
    if (errors || cmdp == 0) return usage_hf_mfu_restore();

    uint8_t *dump = NULL;
    size_t bytes_read = 0;
    if (loadFile_safe(filename, "", (void **)&dump, &bytes_read) != PM3_SUCCESS) {
        PrintAndLogEx(WARNING, "Could not find file " _YELLOW_("%s"), filename);
        return PM3_EIO;
    }

    if (bytes_read < MFU_DUMP_PREFIX_LENGTH) {
        PrintAndLogEx(ERR, "Error, dump file is too small");
        free(dump);
        return PM3_ESOFT;
    }

    int res = convert_mfu_dump_format(&dump, &bytes_read, verbose);
    if (res != PM3_SUCCESS) {
        PrintAndLogEx(FAILED, "Failed convert on load to new Ultralight/NTAG format");
        free(dump);
        return res;
    }

    mfu_dump_t *mem = (mfu_dump_t *)dump;
    uint8_t pages = (bytes_read - MFU_DUMP_PREFIX_LENGTH) / 4;

    if (pages - 1 != mem->pages) {
        PrintAndLogEx(ERR, "Error, invalid dump, wrong page count");
        free(dump);
        return PM3_ESOFT;
    }

    PrintAndLogEx(INFO, "Restoring " _YELLOW_("%s")" to card", filename);

    // print dump
    printMFUdumpEx(mem, pages, 0);

    // Swap endianness
    if (swapEndian && hasKey) {
        if (keylen == 16)
            p_authkey = SwapEndian64(authkey, keylen, 8);
        else
            p_authkey = SwapEndian64(authkey, keylen, 4);
    }

    uint8_t data[20] = {0};
    uint8_t keytype = 0;
    // set key - only once
    if (hasKey) {
        keytype = (keylen == 16) ? 1 : 2;
        memcpy(data + 4, p_authkey, keylen);
    }

    // write version, signature, pack
    // only magic NTAG cards
    if (write_extra) {

#define MFU_NTAG_SPECIAL_PWD        0xF0
#define MFU_NTAG_SPECIAL_PACK       0xF1
#define MFU_NTAG_SPECIAL_VERSION    0xFA
#define MFU_NTAG_SPECIAL_SIGNATURE  0xF2
        // pwd
        if (hasKey || read_key) {

            if (read_key) {
                // try reading key from dump and use.
                memcpy(data, mem->data + (bytes_read - MFU_DUMP_PREFIX_LENGTH - 8), 4);
            } else {
                memcpy(data,  p_authkey, 4);
            }

            PrintAndLogEx(NORMAL, "special PWD     block written 0x%X - %s\n", MFU_NTAG_SPECIAL_PWD, sprint_hex(data, 4));
            clearCommandBuffer();
            SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, MFU_NTAG_SPECIAL_PWD, keytype, 0, data, sizeof(data));

            wait4response(MFU_NTAG_SPECIAL_PWD);

            // copy the new key
            keytype = 2;
            memcpy(authkey, data, 4);
            memcpy(data + 4, authkey, 4);
        }

        // pack
        memcpy(data, mem->data + (bytes_read - MFU_DUMP_PREFIX_LENGTH - 4), 2);
        data[2] = 0;
        data[3] = 0;
        PrintAndLogEx(NORMAL, "special PACK    block written 0x%X - %s\n", MFU_NTAG_SPECIAL_PACK, sprint_hex(data, 4));
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, MFU_NTAG_SPECIAL_PACK, keytype, 0, data, sizeof(data));
        wait4response(MFU_NTAG_SPECIAL_PACK);

        // Signature
        for (uint8_t s = MFU_NTAG_SPECIAL_SIGNATURE, i = 0; s < MFU_NTAG_SPECIAL_SIGNATURE + 8; s++, i += 4) {
            memcpy(data, mem->signature + i, 4);
            PrintAndLogEx(NORMAL, "special SIG     block written 0x%X - %s\n", s, sprint_hex(data, 4));
            clearCommandBuffer();
            SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, s, keytype, 0, data, sizeof(data));
            wait4response(s);
        }

        // Version
        for (uint8_t s = MFU_NTAG_SPECIAL_VERSION, i = 0; s < MFU_NTAG_SPECIAL_VERSION + 2; s++, i += 4) {
            memcpy(data, mem->version + i, 4);
            PrintAndLogEx(NORMAL, "special VERSION block written 0x%X - %s\n", s, sprint_hex(data, 4));
            clearCommandBuffer();
            SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, s, keytype, 0, data, sizeof(data));
            wait4response(s);
        }
    }

    PrintAndLogEx(INFO, "Restoring data blocks.");
    // write all other data
    // Skip block 0,1,2,3 (only magic tags can write to them)
    // Skip last 5 blocks usually is configuration
    for (uint8_t b = 4; b < pages - 5; b++) {

        //Send write Block
        memcpy(data, mem->data + (b * 4), 4);
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, b, keytype, 0, data, sizeof(data));
        wait4response(b);
        PrintAndLogEx(NORMAL, "." NOLF);
    }
    PrintAndLogEx(NORMAL, "\n");

    // write special data last
    if (write_special) {

        PrintAndLogEx(INFO, "Restoring configuration blocks.\n");

        PrintAndLogEx(NORMAL, "authentication with keytype[%x]  %s\n", (uint8_t)(keytype & 0xff), sprint_hex(p_authkey, 4));

        // otp, uid, lock, cfg1, cfg0, dynlockbits
        uint8_t blocks[] = {3, 0, 1, 2, pages - 5, pages - 4, pages - 3};
        for (uint8_t i = 0; i < ARRAYLEN(blocks); i++) {
            uint8_t b = blocks[i];
            memcpy(data, mem->data + (b * 4), 4);
            clearCommandBuffer();
            SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, b, keytype, 0, data, sizeof(data));
            wait4response(b);
            PrintAndLogEx(NORMAL, "special block written %u - %s\n", b, sprint_hex(data, 4));
        }
    }

    DropField();
    free(dump);
    PrintAndLogEx(INFO, "Finish restore");
    return PM3_SUCCESS;
}
//
//  Load emulator with dump file
//
static int CmdHF14AMfUeLoad(const char *Cmd) {
    char c = tolower(param_getchar(Cmd, 0));
    if (c == 'h' || c == 0x00) return usage_hf_mfu_eload();
    return CmdHF14AMfELoad(Cmd);
}
//
//  Simulate tag
//
static int CmdHF14AMfUSim(const char *Cmd) {
    char c = tolower(param_getchar(Cmd, 0));
    if (c == 'h' || c == 0x00) return usage_hf_mfu_sim();
    return CmdHF14ASim(Cmd);
}

//-------------------------------------------------------------------------------
// Ultralight C Methods
//-------------------------------------------------------------------------------

//
// Ultralight C Authentication
//

static int CmdHF14AMfUCAuth(const char *Cmd) {
    uint8_t cmdp = 0;
    char c = tolower(param_getchar(Cmd, 0));
    if (c == 'h') {
        return usage_hf_mfu_ucauth();
    }
    bool keep_field_on = false;
    if (c == 'k') {
        keep_field_on = true;
        cmdp++;
    }

    uint8_t key_buf[16];
    uint8_t *key;
    int succeeded;

    // If no hex key is specified, try all known ones
    if (strlen(Cmd + cmdp) == 0) {
        succeeded = try_default_3des_keys(&key);
        // Else try user-supplied
    } else {
        if (param_gethex(Cmd, cmdp, key_buf, 32)) {
            PrintAndLogEx(WARNING, "Password must include 32 HEX symbols");
            return PM3_EINVARG;
        }
        succeeded = ulc_authentication(key_buf, ! keep_field_on);
        key = key_buf;
    }

    if (succeeded)
        PrintAndLogEx(SUCCESS, "Authentication successful. 3des key: %s", sprint_hex(key, 16));
    else
        PrintAndLogEx(WARNING, "Authentication failed");

    return PM3_SUCCESS;
}

/**
A test function to validate that the polarssl-function works the same
was as the openssl-implementation.
Commented out, since it requires openssl

static int CmdTestDES(const char * cmd)
{
    uint8_t key[16] = {0x00};

    memcpy(key,key3_3des_data,16);
    DES_cblock RndA, RndB;

    PrintAndLogEx(NORMAL, "----------OpenSSL DES implementation----------");
    {
        uint8_t e_RndB[8] = {0x00};
        unsigned char RndARndB[16] = {0x00};

        DES_cblock iv = { 0 };
        DES_key_schedule ks1,ks2;
        DES_cblock key1,key2;

        memcpy(key,key3_3des_data,16);
        memcpy(key1,key,8);
        memcpy(key2,key+8,8);


        DES_set_key((DES_cblock *)key1,&ks1);
        DES_set_key((DES_cblock *)key2,&ks2);

        DES_random_key(&RndA);
        PrintAndLogEx(NORMAL, "     RndA:%s",sprint_hex(RndA, 8));
        PrintAndLogEx(NORMAL, "     e_RndB:%s",sprint_hex(e_RndB, 8));
        //void DES_ede2_cbc_encrypt(const unsigned char *input,
        //    unsigned char *output, long length, DES_key_schedule *ks1,
        //    DES_key_schedule *ks2, DES_cblock *ivec, int enc);
        DES_ede2_cbc_encrypt(e_RndB,RndB,sizeof(e_RndB),&ks1,&ks2,&iv,0);

        PrintAndLogEx(NORMAL, "     RndB:%s",sprint_hex(RndB, 8));
        rol(RndB,8);
        memcpy(RndARndB,RndA,8);
        memcpy(RndARndB+8,RndB,8);
        PrintAndLogEx(NORMAL, "     RA+B:%s",sprint_hex(RndARndB, 16));
        DES_ede2_cbc_encrypt(RndARndB,RndARndB,sizeof(RndARndB),&ks1,&ks2,&e_RndB,1);
        PrintAndLogEx(NORMAL, "enc(RA+B):%s",sprint_hex(RndARndB, 16));

    }
    PrintAndLogEx(NORMAL, "----------PolarSSL implementation----------");
    {
        uint8_t random_a[8]     = { 0 };
        uint8_t enc_random_a[8] = { 0 };
        uint8_t random_b[8]     = { 0 };
        uint8_t enc_random_b[8] = { 0 };
        uint8_t random_a_and_b[16] = { 0 };
        des3_context ctx        = { 0 };

        memcpy(random_a, RndA,8);

        uint8_t output[8]       = { 0 };
        uint8_t iv[8]           = { 0 };

        PrintAndLogEx(NORMAL, "     RndA  :%s",sprint_hex(random_a, 8));
        PrintAndLogEx(NORMAL, "     e_RndB:%s",sprint_hex(enc_random_b, 8));

        des3_set2key_dec(&ctx, key);

        des3_crypt_cbc(&ctx      // des3_context *ctx
            , DES_DECRYPT        // int mode
            , sizeof(random_b)   // size_t length
            , iv                 // unsigned char iv[8]
            , enc_random_b       // const unsigned char *input
            , random_b           // unsigned char *output
            );

        PrintAndLogEx(NORMAL, "     RndB:%s",sprint_hex(random_b, 8));

        rol(random_b,8);
        memcpy(random_a_and_b  ,random_a,8);
        memcpy(random_a_and_b+8,random_b,8);

        PrintAndLogEx(NORMAL, "     RA+B:%s",sprint_hex(random_a_and_b, 16));

        des3_set2key_enc(&ctx, key);

        des3_crypt_cbc(&ctx          // des3_context *ctx
            , DES_ENCRYPT            // int mode
            , sizeof(random_a_and_b)   // size_t length
            , enc_random_b           // unsigned char iv[8]
            , random_a_and_b         // const unsigned char *input
            , random_a_and_b         // unsigned char *output
            );

        PrintAndLogEx(NORMAL, "enc(RA+B):%s",sprint_hex(random_a_and_b, 16));
    }
    return 0;
}
**/

//
// Mifare Ultralight C - Set password
//
static int CmdHF14AMfUCSetPwd(const char *Cmd) {

    uint8_t pwd[16] = {0x00};
    char cmdp = tolower(param_getchar(Cmd, 0));

    if (strlen(Cmd) == 0  || cmdp == 'h') return usage_hf_mfu_ucsetpwd();

    if (param_gethex(Cmd, 0, pwd, 32)) {
        PrintAndLogEx(WARNING, "Password must include 32 HEX symbols");
        return PM3_EINVARG;
    }

    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREUC_SETPWD, 0, 0, 0, pwd, 16);

    PacketResponseNG resp;
    if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        if ((resp.oldarg[0] & 0xff) == 1) {
            PrintAndLogEx(INFO, "Ultralight-C new password: %s", sprint_hex(pwd, 16));
        } else {
            PrintAndLogEx(WARNING, "Failed writing at block %u", (uint8_t)(resp.oldarg[1] & 0xff));
            return PM3_ESOFT;
        }
    } else {
        PrintAndLogEx(WARNING, "command execution time out");
        return PM3_ETIMEOUT;
    }
    return PM3_SUCCESS;
}

//
// Magic UL / UL-C tags  - Set UID
//
static int CmdHF14AMfUCSetUid(const char *Cmd) {

    PacketResponseNG resp;
    uint8_t uid[7] = {0x00};
    char cmdp = tolower(param_getchar(Cmd, 0));

    if (strlen(Cmd) == 0  || cmdp == 'h') return usage_hf_mfu_ucsetuid();

    if (param_gethex(Cmd, 0, uid, 14)) {
        PrintAndLogEx(WARNING, "UID must include 14 HEX symbols");
        return PM3_EINVARG;
    }

    PrintAndLogEx(INFO, "Please ignore possible transient BCC warnings");
    // read block2.
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_READBL, 2, 0, 0, NULL, 0);
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        PrintAndLogEx(WARNING, "Command execute timeout");
        return PM3_ETIMEOUT;
    }

    // save old block2.
    uint8_t oldblock2[4] = {0x00};
    memcpy(resp.data.asBytes, oldblock2, 4);

    // Enforce bad BCC handling temporarily as BCC will be wrong between
    // block 1 write and block2 write
    hf14a_config config;
    SendCommandNG(CMD_HF_ISO14443A_GET_CONFIG, NULL, 0);
    if (!WaitForResponseTimeout(CMD_HF_ISO14443A_GET_CONFIG, &resp, 2000)) {
        PrintAndLogEx(WARNING, "command execution time out");
        return PM3_ETIMEOUT;
    }
    memcpy(&config, resp.data.asBytes, sizeof(hf14a_config));
    int8_t oldconfig_bcc = config.forcebcc;
    if (oldconfig_bcc != 2) {
        config.forcebcc = 2;
        SendCommandNG(CMD_HF_ISO14443A_SET_CONFIG, (uint8_t *)&config, sizeof(hf14a_config));
    }

    // block 0.
    uint8_t data[4];
    data[0] = uid[0];
    data[1] = uid[1];
    data[2] = uid[2];
    data[3] =  0x88 ^ uid[0] ^ uid[1] ^ uid[2];
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, 0, 0, 0, data, sizeof(data));
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        PrintAndLogEx(WARNING, "Command execute timeout");
        return PM3_ETIMEOUT;
    }

    // block 1.
    data[0] = uid[3];
    data[1] = uid[4];
    data[2] = uid[5];
    data[3] = uid[6];
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, 1, 0, 0, data, sizeof(data));
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        PrintAndLogEx(WARNING, "Command execute timeout");
        return PM3_ETIMEOUT;
    }

    // block 2.
    data[0] = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];
    data[1] = oldblock2[1];
    data[2] = oldblock2[2];
    data[3] = oldblock2[3];
    clearCommandBuffer();
    SendCommandMIX(CMD_HF_MIFAREU_WRITEBL, 2, 0, 0, data, sizeof(data));
    if (!WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
        PrintAndLogEx(WARNING, "Command execute timeout");
        return PM3_ETIMEOUT;
    }

    // restore BCC config
    if (oldconfig_bcc != 2) {
        config.forcebcc = oldconfig_bcc;
        SendCommandNG(CMD_HF_ISO14443A_SET_CONFIG, (uint8_t *)&config, sizeof(hf14a_config));
    }
    return PM3_SUCCESS;
}

static int CmdHF14AMfUGenDiverseKeys(const char *Cmd) {

    uint8_t uid[4];
    char cmdp = tolower(param_getchar(Cmd, 0));
    if (strlen(Cmd) == 0  || cmdp == 'h') return usage_hf_mfu_gendiverse();

    if (cmdp == 'r') {
        // read uid from tag
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_RATS, 0, 0, NULL, 0);
        PacketResponseNG resp;
        WaitForResponse(CMD_ACK, &resp);
        iso14a_card_select_t card;
        memcpy(&card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

        uint64_t select_status = resp.oldarg[0];
        // 0: couldn't read,
        // 1: OK, with ATS
        // 2: OK, no ATS
        // 3: proprietary Anticollision

        if (select_status == 0) {
            PrintAndLogEx(WARNING, "iso14443a card select failed");
            return PM3_ESOFT;
        }
        /*
                if (card.uidlen != 4) {
                    PrintAndLogEx(WARNING, "Wrong sized UID, expected 4bytes got %d", card.uidlen);
                    return PM3_ESOFT;
                }
        */
        memcpy(uid, card.uid, card.uidlen);
    } else {
        if (param_gethex(Cmd, 0, uid, 8)) return usage_hf_mfu_gendiverse();
    }


    uint8_t iv[8] = { 0x00 };
    uint8_t block = 0x01;

    uint8_t mifarekeyA[] = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
    uint8_t mifarekeyB[] = { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5 };
    uint8_t dkeyA[8] = { 0x00 };
    uint8_t dkeyB[8] = { 0x00 };

    uint8_t masterkey[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
    uint8_t mix[8] = { 0x00 };
    uint8_t divkey[8] = { 0x00 };

    memcpy(mix, mifarekeyA, 4);

    mix[4] = mifarekeyA[4] ^ uid[0];
    mix[5] = mifarekeyA[5] ^ uid[1];
    mix[6] = block ^ uid[2];
    mix[7] = uid[3];

    mbedtls_des3_context ctx;
    mbedtls_des3_set2key_enc(&ctx, masterkey);

    mbedtls_des3_crypt_cbc(&ctx  // des3_context
                           , MBEDTLS_DES_ENCRYPT    // int mode
                           , sizeof(mix)    // length
                           , iv             // iv[8]
                           , mix            // input
                           , divkey         // output
                          );

    PrintAndLogEx(SUCCESS, "-- 3DES version");
    PrintAndLogEx(SUCCESS, "Masterkey    :\t %s", sprint_hex(masterkey, sizeof(masterkey)));
    PrintAndLogEx(SUCCESS, "UID          :\t %s", sprint_hex(uid, sizeof(uid)));
    PrintAndLogEx(SUCCESS, "block        :\t %0d", block);
    PrintAndLogEx(SUCCESS, "Mifare key   :\t %s", sprint_hex(mifarekeyA, sizeof(mifarekeyA)));
    PrintAndLogEx(SUCCESS, "Message      :\t %s", sprint_hex(mix, sizeof(mix)));
    PrintAndLogEx(SUCCESS, "Diversified key: %s", sprint_hex(divkey + 1, 6));

    for (int i = 0; i < ARRAYLEN(mifarekeyA); ++i) {
        dkeyA[i]  = (mifarekeyA[i] << 1) & 0xff;
        dkeyA[6] |= ((mifarekeyA[i] >> 7) & 1) << (i + 1);
    }

    for (int i = 0; i < ARRAYLEN(mifarekeyB); ++i) {
        dkeyB[1]  |= ((mifarekeyB[i] >> 7) & 1) << (i + 1);
        dkeyB[2 + i] = (mifarekeyB[i] << 1) & 0xff;
    }

    uint8_t zeros[8] = {0x00};
    uint8_t newpwd[8] = {0x00};
    uint8_t dmkey[24] = {0x00};
    memcpy(dmkey, dkeyA, 8);
    memcpy(dmkey + 8, dkeyB, 8);
    memcpy(dmkey + 16, dkeyA, 8);
    memset(iv, 0x00, 8);

    mbedtls_des3_set3key_enc(&ctx, dmkey);

    mbedtls_des3_crypt_cbc(&ctx  // des3_context
                           , MBEDTLS_DES_ENCRYPT    // int mode
                           , sizeof(newpwd) // length
                           , iv             // iv[8]
                           , zeros         // input
                           , newpwd         // output
                          );

    PrintAndLogEx(SUCCESS, "\n-- DES version");
    PrintAndLogEx(SUCCESS, "Mifare dkeyA :\t %s", sprint_hex(dkeyA, sizeof(dkeyA)));
    PrintAndLogEx(SUCCESS, "Mifare dkeyB :\t %s", sprint_hex(dkeyB, sizeof(dkeyB)));
    PrintAndLogEx(SUCCESS, "Mifare ABA   :\t %s", sprint_hex(dmkey, sizeof(dmkey)));
    PrintAndLogEx(SUCCESS, "Mifare Pwd   :\t %s", sprint_hex(newpwd, sizeof(newpwd)));

    mbedtls_des3_free(&ctx);

    mbedtls_aes_context ctx_aes;
    uint8_t aes_iv[16] = { 0x00 };
    uint8_t aes_masterkey[] = { 0x00, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F };
    uint8_t aes_input[16] = {0x01, 0x04, 0x2A, 0x2E, 0x19, 0x70, 0x1C, 0x80, 0x01, 0x04, 0x2A, 0x2E, 0x19, 0x70, 0x1C, 0x80};
    uint8_t aes_output[16] = {0x00};
    mbedtls_aes_setkey_enc(&ctx_aes, aes_masterkey, 128);
    mbedtls_aes_crypt_cbc(&ctx_aes, MBEDTLS_AES_ENCRYPT, 16, aes_iv, aes_input, aes_output);
    mbedtls_aes_free(&ctx_aes);

    PrintAndLogEx(SUCCESS, "\n-- AES version");
    PrintAndLogEx(SUCCESS, "Mifare AES m :\t %s", sprint_hex(aes_masterkey, sizeof(aes_masterkey)));
    PrintAndLogEx(SUCCESS, "Mifare Div   :\t %s", sprint_hex(aes_output, sizeof(aes_output)));

    // next. from the diversify_key method.
    return PM3_SUCCESS;
}

static int CmdHF14AMfUPwdGen(const char *Cmd) {

    uint8_t uid[7] = {0x00};
    char cmdp = tolower(param_getchar(Cmd, 0));
    if (strlen(Cmd) == 0  || cmdp == 'h') return usage_hf_mfu_pwdgen();

    if (cmdp == 't') return generator_selftest();

    if (cmdp == 'r') {
        // read uid from tag
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_ISO14443A_READER, ISO14A_CONNECT | ISO14A_NO_RATS, 0, 0, NULL, 0);
        PacketResponseNG resp;
        WaitForResponse(CMD_ACK, &resp);
        iso14a_card_select_t card;
        memcpy(&card, (iso14a_card_select_t *)resp.data.asBytes, sizeof(iso14a_card_select_t));

        uint64_t select_status = resp.oldarg[0];
        // 0: couldn't read
        // 1: OK with ATS
        // 2: OK, no ATS
        // 3: proprietary Anticollision
        if (select_status == 0) {
            PrintAndLogEx(WARNING, "iso14443a card select failed");
            return PM3_ESOFT;
        }
        if (card.uidlen != 7) {
            PrintAndLogEx(WARNING, "Wrong sized UID, expected 7bytes got %d", card.uidlen);
            return PM3_ESOFT;
        }
        memcpy(uid, card.uid, sizeof(uid));
    } else {
        if (param_gethex(Cmd, 0, uid, 14)) return usage_hf_mfu_pwdgen();
    }

    PrintAndLogEx(NORMAL, "---------------------------------");
    PrintAndLogEx(NORMAL, " Using UID : %s", sprint_hex(uid, 7));
    PrintAndLogEx(NORMAL, "---------------------------------");
    PrintAndLogEx(NORMAL, " algo | pwd      | pack");
    PrintAndLogEx(NORMAL, "------+----------+-----");
    PrintAndLogEx(NORMAL, " EV1  | %08X | %04X", ul_ev1_pwdgenA(uid), ul_ev1_packgenA(uid));
    PrintAndLogEx(NORMAL, " Ami  | %08X | %04X", ul_ev1_pwdgenB(uid), ul_ev1_packgenB(uid));
    PrintAndLogEx(NORMAL, " LD   | %08X | %04X", ul_ev1_pwdgenC(uid), ul_ev1_packgenC(uid));
    PrintAndLogEx(NORMAL, " XYZ  | %08X | %04X", ul_ev1_pwdgenD(uid), ul_ev1_packgenD(uid));
    PrintAndLogEx(NORMAL, "------+----------+-----");
    PrintAndLogEx(NORMAL, " Vingcard algo");
    PrintAndLogEx(NORMAL, "--------------------");
    return PM3_SUCCESS;
}

//
// MFU TearOff against OTP
// Moebius et al
//
static int CmdHF14AMfuOtpTearoff(const char *Cmd) {
    uint8_t blockNoUint = 8;
    uint8_t cmdp = 0;
    bool errors = 0, use_match = false;
    uint8_t match[4] = {0x00};
    uint8_t teardata[8] = {0x00};
    uint32_t interval = 500; // time in us
    uint32_t timeLimit = 3000; // time in us
    uint32_t startTime = 0; // time in us

    while (param_getchar(Cmd, cmdp) != 0x00 && !errors) {
        switch (tolower(param_getchar(Cmd, cmdp))) {
            case 'h':
                return usage_hf_mfu_otp_tearoff();
            case 'b':
                blockNoUint = param_get8(Cmd, cmdp + 1);
                if (blockNoUint < 2) {
                    PrintAndLogEx(WARNING, "Wrong block number");
                    errors = true;
                }
                cmdp += 2;
                break;
            case 'i':
                interval = param_get32ex(Cmd, cmdp + 1, interval, 10);
                cmdp += 2;
                break;
            case 'l':
                timeLimit = param_get32ex(Cmd, cmdp + 1, timeLimit, 10);
                if (timeLimit < interval) {
                    PrintAndLogEx(WARNING, "Wrong time limit number");
                    errors = true;
                }
                if (timeLimit > 43000) {
                    PrintAndLogEx(WARNING, "You can't set delay out of 1..43000 range!");
                    errors = true;
                }
                cmdp += 2;
                break;
            case 's':
                startTime = param_get32ex(Cmd, cmdp + 1, 0, 10);
                if (startTime > (timeLimit - interval)) {
                    PrintAndLogEx(WARNING, "Wrong start time number");
                    errors = true;
                }
                cmdp += 2;
                break;
            case 'd':
                if (param_gethex(Cmd, cmdp + 1, teardata, 8)) {
                    PrintAndLogEx(WARNING, "Block data must include 8 HEX symbols");
                    errors = true;
                }
                cmdp += 2;
                break;
            case 't':
                if (param_gethex(Cmd, cmdp + 1, teardata + 4, 8)) {
                    PrintAndLogEx(WARNING, "Block data must include 8 HEX symbols");
                    errors = true;
                }
                cmdp += 2;
                break;
            case 'm':
                if (param_gethex(Cmd, cmdp + 1, match, 8)) {
                    PrintAndLogEx(WARNING, "Block data must include 8 HEX symbols");
                    errors = true;
                }
                use_match = true;
                cmdp += 2;
                break;
            default:
                PrintAndLogEx(WARNING, "Unknown parameter '%c'", param_getchar(Cmd, cmdp));
                errors = true;
                break;
        }
    }

    if (errors) return usage_hf_mfu_otp_tearoff();

    PrintAndLogEx(INFO, "----------------- " _CYAN_("MFU Tear off") " ---------------------");
    PrintAndLogEx(INFO, "Starting Tear-off test");
    PrintAndLogEx(INFO, "Target block no: %u", blockNoUint);
    PrintAndLogEx(INFO, "Target inital block data : %s", sprint_hex_inrow(teardata, 4));
    PrintAndLogEx(INFO, "Target write block data  : %s", sprint_hex_inrow(teardata + 4, 4));
    PrintAndLogEx(INFO, "----------------------------------------------------");
    uint8_t isOK;
    bool got_pre = false, got_post = false, lock_on = false;
    uint8_t pre[4] = {0};
    uint8_t post[4] = {0};
    uint32_t actualTime = startTime;

    int phase_clear = -1;
    int phase_newwr = -1;

    uint8_t retries = 0;
    while (actualTime <= (timeLimit - interval)) {

        if (kbd_enter_pressed()) {
            PrintAndLogEx(INFO, "\naborted via keyboard!\n");
            break;
        }

        PrintAndLogEx(INFO, "Using tear-off delay " _GREEN_("%" PRIu32) " us", actualTime);

        clearCommandBuffer();
        SendCommandMIX(CMD_HF_MIFAREU_READBL, blockNoUint, 0, 0, NULL, 0);
        PacketResponseNG resp;

        got_pre = false;
        if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
            isOK = resp.oldarg[0] & 0xFF;
            if (isOK) {
                memcpy(pre, resp.data.asBytes, sizeof(pre));
                got_pre = true;
            }
        }

        clearCommandBuffer();
        SendCommandMIX(CMD_HF_MFU_OTP_TEAROFF, blockNoUint, actualTime, 0, teardata, 8);

        // we be getting ACK that we are silently ignoring here..

        if (!WaitForResponseTimeout(CMD_HF_MFU_OTP_TEAROFF, &resp, 2000)) {
            PrintAndLogEx(WARNING, "Failed");
            return PM3_ESOFT;
        }

        if (resp.status != PM3_SUCCESS) {
            PrintAndLogEx(WARNING, "Tear off reporting failure to select tag");
            continue;
        }

        got_post = false;
        clearCommandBuffer();
        SendCommandMIX(CMD_HF_MIFAREU_READBL, blockNoUint, 0, 0, NULL, 0);
        if (WaitForResponseTimeout(CMD_ACK, &resp, 1500)) {
            isOK = resp.oldarg[0] & 0xFF;
            if (isOK) {
                memcpy(post, resp.data.asBytes, sizeof(post));
                got_post = true;
            }
        }

        if (got_pre && got_post) {

            char prestr[20] = {0};
            snprintf(prestr, sizeof(prestr), "%s", sprint_hex_inrow(pre, sizeof(pre)));
            char poststr[20] = {0};
            snprintf(poststr, sizeof(poststr), "%s", sprint_hex_inrow(post, sizeof(post)));

            if (memcmp(pre, post, sizeof(pre)) == 0) {

                PrintAndLogEx(INFO, "Current %02d (0x%02X) %s"
                              , blockNoUint
                              , blockNoUint
                              , poststr
                             );
            } else {

                // skip first message, since its the reset write.
                if (actualTime == startTime) {
                    PrintAndLogEx(INFO, "Inital write");
                } else {
                    PrintAndLogEx(INFO, _CYAN_("Tear off occured") " : %02d (0x%02X) %s vs " _RED_("%s")
                                  , blockNoUint
                                  , blockNoUint
                                  , prestr
                                  , poststr
                                 );

                    lock_on = true;

                    if (phase_clear == -1)
                        phase_clear = actualTime;

                    // new write phase must be atleast 100us later..
                    if (phase_clear > -1 && phase_newwr == -1 && actualTime > (phase_clear + 100))
                        phase_newwr = actualTime;
                }
            }

            if (use_match && memcmp(pre, match, sizeof(pre)) == 0) {
                PrintAndLogEx(SUCCESS, "Block matches!\n");
                break;
            }

        } else {
            if (got_pre == false)
                PrintAndLogEx(FAILED, "Failed to read block BEFORE");
            if (got_post == false)
                PrintAndLogEx(FAILED, "Failed to read block AFTER");
        }

        /*  TEMPORALLY DISABLED
                uint8_t d0, d1, d2, d3;
                d0 = *resp.data.asBytes;
                d1 = *(resp.data.asBytes + 1);
                d2 = *(resp.data.asBytes + 2);
                d3 = *(resp.data.asBytes + 3);
                if ((d0 != 0xFF) || (d1 != 0xFF) || (d2 != 0xFF) || (d3 = ! 0xFF)) {
                    PrintAndLogEx(NORMAL, "---------------------------------");
                    PrintAndLogEx(NORMAL, "        EFFECT AT: %d us", actualTime);
                    PrintAndLogEx(NORMAL, "---------------------------------\n");
                }
        */
        if (startTime != timeLimit) {
            actualTime += interval;
        } else {
            if (lock_on == false) {
                if (++retries == 20) {
                    actualTime++;
                    timeLimit++;
                    startTime++;
                    retries = 0;
                    PrintAndLogEx(INFO, _CYAN_("Retried %u times, increased delay with 1us"), retries);
                }
            }
        }
    }

    PrintAndLogEx(INFO, "----------------------------------------------------");
    if (phase_clear > - 1) {
        PrintAndLogEx(INFO, "New phase boundary around " _YELLOW_("%d") " us", phase_clear);
    }
    if (phase_newwr > - 1) {
        PrintAndLogEx(INFO, "New phase boundary around " _YELLOW_("%d") " us", phase_newwr);
    }
    PrintAndLogEx(NORMAL, "");
    return PM3_SUCCESS;
}

/*
static int counter_reset_tear(iso14a_card_select_t *card, uint8_t cnt_no) {

    PrintAndLogEx(INFO, "Reset tear check");

    uint8_t cw[6] = { MIFARE_ULEV1_INCR_CNT, cnt_no, 0x00, 0x00, 0x00, 0x00};
    uint8_t ct[1] = {0};
    uint8_t resp[10] = {0};

    if (ul_select(card) == false) {
        PrintAndLogEx(FAILED, "failed to select card,  exiting...");
        return PM3_ESOFT;
    }
    if (ul_send_cmd_raw(cw, sizeof(cw), resp, sizeof(resp)) < 0) {
        PrintAndLogEx(FAILED, "failed to write all ZEROS");
        return PM3_ESOFT;
    }
    if (ulev1_readTearing(cnt_no, ct, sizeof(ct)) < 0) {
        PrintAndLogEx(FAILED, "AFTER, failed to read ANTITEAR,  exiting...");
        return PM3_ESOFT;
    }
    DropField();

    if (ct[0] != 0xBD) {
        PrintAndLogEx(INFO, "Resetting seem to have failed, WHY!?");
        return PM3_ESOFT;
    }
    return PM3_SUCCESS;
}
*/

/*
static int CmdHF14AMfuEv1CounterTearoff(const char *Cmd) {

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu countertear",
                  "Tear-off test against a Ev1 counter",
                  "hf mfu countertear\n"
                  "hf mfu countertear -s 200 -l 2500      -> target counter 0, start delay 200\n"
                  "hf mfu countertear -i 2 -s 200 -l 400  -> target counter 0, start delay 200\n"
                 );

    void *argtable[] = {
        arg_param_begin,
        arg_int0("c", "cnt", "<0,1,2>", "Target this EV1 counter (0,1,2)"),
        arg_int0("i", "inc", "<dec>", "time interval to increase in each iteration - default 10 us"),
        arg_int0("l", "limit", "<dec>", "test upper limit time - default 3000 us"),
        arg_int0("s", "start", "<dec>", "test start time - default 0 us"),
        arg_int0(NULL, "fix", "<dec>", "test fixed loop delay"),
        arg_str0("x", "hex",  NULL, "3 byte hex to increase counter with"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, false);

    int interval = 0;
    int time_limit, start_time = 0;
    int counter = arg_get_int_def(ctx, 1, 0);
    int fixed = arg_get_int_def(ctx, 5, -1);

    if ( fixed == -1 ) {
        interval = arg_get_int_def(ctx, 2, 10);
        time_limit = arg_get_int_def(ctx, 3, 3000);
        start_time = arg_get_int_def(ctx, 4, 0);
    } else {
        start_time = fixed;
        interval = 0;
        time_limit = fixed;
    }

    uint8_t newvalue[5] = {0};
    int newvaluelen = 0;
    CLIGetHexWithReturn(ctx, 6, newvalue, &newvaluelen);
    CLIParserFree(ctx);

    // Validations
    if (start_time > (time_limit - interval)) {
        PrintAndLogEx(WARNING, "Wrong start time number");
        return PM3_EINVARG;
    }
    if (time_limit < interval) {
        PrintAndLogEx(WARNING, "Wrong time limit number");
        return PM3_EINVARG;
    }
    if (time_limit > 43000) {
        PrintAndLogEx(WARNING, "You can't set delay out of 1..43000 range!");
        return PM3_EINVARG;
    }
    uint8_t cnt_no = 0;
    if (counter < 0 || counter > 2) {
        PrintAndLogEx(WARNING, "Counter must 0, 1 or 2");
        return PM3_EINVARG;
    }

    cnt_no = (uint8_t)counter;

    iso14a_card_select_t card;

    // reset counter tear
    counter_reset_tear(&card, cnt_no);

    if (ul_select(&card) == false) {
        PrintAndLogEx(INFO, "failed to select card,  exiting...");
        return PM3_ESOFT;
    }

    uint8_t inital_cnt[3] = {0, 0, 0};
    int len = ulev1_readCounter(cnt_no, inital_cnt, sizeof(inital_cnt));
    if ( len != sizeof(inital_cnt) ) {
        PrintAndLogEx(WARNING, "failed to read counter");
        return PM3_ESOFT;
    }

    uint8_t inital_tear[1] = {0};
    len = ulev1_readTearing(cnt_no, inital_tear, sizeof(inital_tear));
    DropField();
    if ( len != sizeof(inital_tear) ) {
        PrintAndLogEx(WARNING, "failed to read ANTITEAR,  exiting...  %d", len);
        return PM3_ESOFT;
    }

    uint32_t wr_value = ( newvalue[0] | newvalue[1] << 8 | newvalue[2] << 16 );
    uint32_t inital_value = ( inital_cnt[0] | inital_cnt[1] << 8 | inital_cnt[2] << 16 );;

    PrintAndLogEx(INFO, "----------------- " _CYAN_("MFU Ev1 Counter Tear off") " ---------------------");
    PrintAndLogEx(INFO, "Target counter no     [ " _GREEN_("%u") " ]", counter);
    PrintAndLogEx(INFO, "       counter value  [ " _GREEN_("%s") " ]", sprint_hex_inrow(inital_cnt, sizeof(inital_cnt)));
    PrintAndLogEx(INFO, "     anti-tear value  [ " _GREEN_("%02X") " ]", inital_tear[0]);
    PrintAndLogEx(INFO, "       increase value [ " _GREEN_("%s") " ]", sprint_hex_inrow(newvalue, newvaluelen));
    PrintAndLogEx(INFO, "----------------------------------------------------");

    uint8_t pre_tear = 0, post_tear = 0;
    uint8_t pre[3] = {0};
    uint8_t post[3] = {0};
    uint32_t actual_time = start_time;
    uint32_t a = 0, b = 0;
    uint32_t loop = 0;

    uint16_t late = 0;

    while (actual_time <= (time_limit - interval)) {


        DropField();

        loop++;

        if (kbd_enter_pressed()) {
            PrintAndLogEx(INFO, "\naborted via keyboard!\n");
            break;
        }

        PrintAndLogEx(INPLACE, "Using tear-off delay " _GREEN_("%" PRIu32) " us  (attempt %u)", actual_time, loop);

        if (ul_select(&card) == false) {
            PrintAndLogEx(FAILED, "BEFORE, failed to select card,  looping...");
            continue;
        }

        uint8_t cntresp[3] = {0, 0, 0};
        int rlen = ulev1_readCounter(cnt_no, cntresp, sizeof(cntresp));
        if ( rlen == sizeof(cntresp) ) {
            memcpy(pre, cntresp, sizeof(pre));
        } else {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(FAILED, "BEFORE, failed to read COUNTER,  exiting...");
            break;
        }

        uint8_t tear[1] = {0};
        int tlen = ulev1_readTearing(cnt_no, tear, sizeof(tear));
        if ( tlen == sizeof(tear) ) {
            pre_tear = tear[0];
        } else {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(FAILED, "BEFORE, failed to read ANTITEAR,  exiting...  %d", tlen);
            break;
        }

        DropField();

        struct p {
            uint8_t counter;
            uint32_t tearoff_time;
            uint8_t value[3];
        } PACKED payload;
        payload.counter = cnt_no;
        payload.tearoff_time = actual_time;
        memcpy(payload.value, newvalue, sizeof(payload.value));

        clearCommandBuffer();
        PacketResponseNG resp;
        SendCommandNG(CMD_HF_MFU_COUNTER_TEAROFF, (uint8_t*)&payload, sizeof(payload));
        if (!WaitForResponseTimeout(CMD_HF_MFU_COUNTER_TEAROFF, &resp, 2000)) {
            PrintAndLogEx(WARNING, "\ntear off command failed");
            continue;
        }

        if (ul_select(&card) == false) {
            PrintAndLogEx(FAILED, "AFTER, failed to select card,  exiting...");
            break;
        }

        rlen = ulev1_readCounter(cnt_no, cntresp, sizeof(cntresp));
        if ( rlen == sizeof(cntresp) ) {
            memcpy(post, cntresp, sizeof(post));
        } else {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(FAILED, "AFTER, failed to read COUNTER,  exiting...");
            break;
        }

        tear[0] = 0;
        tlen = ulev1_readTearing(cnt_no, tear, sizeof(tear));
        if ( tlen == sizeof(tear) ) {
            post_tear = tear[0];
        } else {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(FAILED, "AFTER, failed to read ANTITEAR,  exiting...");
            break;
        }

        DropField();

        char prestr[20] = {0};
        snprintf(prestr, sizeof(prestr), "%s", sprint_hex_inrow(pre, sizeof(pre)));
        char poststr[20] = {0};
        snprintf(poststr, sizeof(poststr), "%s", sprint_hex_inrow(post, sizeof(post)));

        bool post_tear_check = (post_tear == 0xBD);
        a = (pre[0] | pre[1] << 8 | pre[2]  << 16);
        b = (post[0] | post[1] << 8 | post[2]  << 16);

        // A != B
        if (memcmp(pre, post, sizeof(pre)) != 0) {


            PrintAndLogEx(NORMAL, "");

            if (inital_value != a ) {

                if ( inital_value != b )
                    PrintAndLogEx(INFO, "pre %08x, post %08x != inital %08x  |  tear:  0x%02X  == 0x%02X", a, b, inital_value, pre_tear, post_tear);
                else
                    PrintAndLogEx(INFO, "pre %08x != inital and post %08x == inital %08x |  tear:  0x%02X  == 0x%02X", a, b, inital_value, pre_tear, post_tear);
            } else {

                if ( inital_value != b )
                    PrintAndLogEx(INFO, "pre %08x == inital and post %08x != inital  %08x |  tear:  0x%02X  == 0x%02X", a, b, inital_value, pre_tear, post_tear);
            }

            if ( b == 0 ) {
                PrintAndLogEx(INFO, _CYAN_("Tear off occured  (ZEROS value!) ->  ") "%s vs " _GREEN_("%s") "  Tear status:  0x%02X == 0x%02X   ( %s )"
                    , prestr
                    , poststr
                    , pre_tear
                    , post_tear
                    , post_tear_check ? _GREEN_("OK") : _RED_("DETECTED")
                );
                break;
            }

            if ( a > b ) {
                PrintAndLogEx(INFO, _CYAN_("Tear off occured  " _RED_("( LESS )") " ->  ") "%s vs " _GREEN_("%s") "  Tear status:  0x%02X == 0x%02X   ( %s )"
                    , prestr
                    , poststr
                    , pre_tear
                    , post_tear
                    , post_tear_check ? _GREEN_("OK") : _RED_("DETECTED")
                );


                if (counter_reset_tear(&card, cnt_no) != PM3_SUCCESS){
                    PrintAndLogEx(FAILED, "failed to reset tear,  exiting...");
                    break;
                }

                uint32_t bar =  (0x1000000 - b) + 2;
                // wr_value = bar;
                // newvalue[0] = (bar) & 0xFF;
                // newvalue[1] = ((bar >> 8) & 0xFF);
                // newvalue[2] = ((bar >> 16) & 0xFF);

                wr_value = 0;
                newvalue[0] = 0;
                newvalue[1] = 0;
                newvalue[2] = 0;

                PrintAndLogEx(INFO, "     0x1000000 - 0x%x == 0x%x", b, bar);
                PrintAndLogEx(INFO, "      new increase value 0x%x" , wr_value);
                PrintAndLogEx(INFO, "    because BAR + post == 0x%x" , bar + b);

                PrintAndLogEx(INFO, "New increase value " _YELLOW_("%s"), sprint_hex_inrow(newvalue, newvaluelen));
                continue;
            } else  {

                PrintAndLogEx(NORMAL, "");
                PrintAndLogEx(INFO, _CYAN_("Tear off occured  (+1)  (too late) ->  ") "%s vs %s   Tear:  0x%02X == 0x%02X   ( %s )"
                    , prestr
                    , poststr
                    , pre_tear
                    , post_tear
                    , post_tear_check ? _GREEN_("OK") : _RED_("DETECTED")
                );

                if ( post_tear_check  && b == inital_value) {
                    PrintAndLogEx(INFO, "Reverted to previous value");
                    break;
                }
                if ( wr_value != 0 ) {

                    //uint32_t bar =  (0x1000000 - b) + 2;
                    wr_value = 0;
                    newvalue[0] = 0;
                    newvalue[1] = 0;
                    newvalue[2] = 0;

                    if ( b >= (inital_value + (2 * wr_value))) {
                        PrintAndLogEx(INFO, "Large " _YELLOW_("( JUMP )") " detected");


                        // wr_value = bar;
                        // newvalue[0] = (bar) & 0xFF;
                        // newvalue[1] = ((bar >> 8) & 0xFF);
                        // newvalue[2] = ((bar >> 16) & 0xFF);
                    } else {

                        // wr_value = bar;
                        // newvalue[0] = (bar) & 0xFF;
                        // newvalue[1] = ((bar >> 8) & 0xFF);
                        // newvalue[2] = ((bar >> 16) & 0xFF);
                        // wr_value = 0;
                        // newvalue[0] = 0;
                        // newvalue[1] = 0;
                        // newvalue[2] = 0;
                    }

                }
                PrintAndLogEx(INFO, "New increase value " _YELLOW_("%s"), sprint_hex_inrow(newvalue, newvaluelen));

                //actual_time--;
                late++;
            }
        } else {
            PrintAndLogEx(NORMAL, "");
            PrintAndLogEx(INFO, _CYAN_("Status:  same value!   ->  ") "%s == %s   Tear:  0x%02X == 0x%02X   ( %s )"
                , prestr
                , poststr
                , pre_tear
                , post_tear
                , post_tear_check ? _GREEN_("OK") : _RED_("DETECTED")
            );

            if ( post_tear_check ) {
                if ( a == b ) {
                    //actual_time--;
                    continue;
                }

                if ( b == inital_value ) {
                    PrintAndLogEx(INFO, "Reverted to previous value");
                    break;
                }
            } else {

                if (counter_reset_tear(&card, cnt_no) != PM3_SUCCESS){
                    PrintAndLogEx(FAILED, "failed to reset tear,  exiting...");
                    break;
                }

            }
        }

        actual_time += interval;
    }

    DropField();

    PrintAndLogEx(INFO, " Sent %u tear offs ", loop);

    counter_reset_tear(&card, cnt_no);

    PrintAndLogEx(INFO, "hf 14a raw -s -c 3900              -->  read counter 0");
    PrintAndLogEx(INFO, "hf 14a raw -s -c 3e00              -->  read tearing 0");
    PrintAndLogEx(NORMAL, "");
    char read_cnt_str[30];
    sprintf(read_cnt_str, "hf 14a raw -s -c 39%02x", counter);
    CommandReceived(read_cnt_str);
    char read_tear_str[30];
    sprintf(read_tear_str, "hf 14a raw -s -c 3e%02x", counter);
    CommandReceived(read_tear_str);
    return PM3_SUCCESS;
}

*/

static int CmdHF14MfuNDEF(const char *Cmd) {

    int keylen;
    int maxsize = 16, status;
    bool hasAuthKey = false;
    bool swapEndian = false;

    iso14a_card_select_t card;
    uint8_t data[16] = {0x00};
    uint8_t key[16] = {0x00};
    uint8_t *p_key = key;
    uint8_t pack[4] = {0, 0, 0, 0};

    CLIParserContext *ctx;
    CLIParserInit(&ctx, "hf mfu ndef",
                  "Prints NFC Data Exchange Format (NDEF)",
                  "hf mfu ndef -> shows NDEF data\n"
                  "hf mfu ndef -k ffffffff -> shows NDEF data with key");

    void *argtable[] = {
        arg_param_begin,
        arg_str0("k", "key", "replace default key for NDEF", NULL),
        arg_lit0("l", "key", "(optional) swap entered key's endianness"),
        arg_param_end
    };
    CLIExecWithReturn(ctx, Cmd, argtable, true);
    CLIGetHexWithReturn(ctx, 1, key, &keylen);
    swapEndian = arg_get_lit(ctx, 2);
    CLIParserFree(ctx);

    switch (keylen) {
        case 0:
            break;
        case 4:
        case 16:
            hasAuthKey = true;
            break;
        default:
            PrintAndLogEx(WARNING, "ERROR: Key is incorrect length\n");
            return PM3_EINVARG;
    }

    // Get tag type
    TagTypeUL_t tagtype = GetHF14AMfU_Type();
    if (tagtype == UL_ERROR) {
        PrintAndLogEx(WARNING, "No Ultraligth / NTAG based tag found");
        return PM3_ESOFT;
    }

    // Is tag UL/NTAG?

    // Swap endianness
    if (swapEndian && hasAuthKey) p_key = SwapEndian64(key, keylen, (keylen == 16) ? 8 : 4);

    // Select and Auth
    if (ul_auth_select(&card, tagtype, hasAuthKey, p_key, pack, sizeof(pack)) == PM3_ESOFT) return PM3_ESOFT;

    // read pages 0,1,2,3 (should read 4pages)
    status = ul_read(0, data, sizeof(data));
    if (status == -1) {
        DropField();
        PrintAndLogEx(ERR, "Error: tag didn't answer to READ");
        return PM3_ESOFT;
    } else if (status == 16) {

        status = ndef_print_CC(data + 12);
        if (status == PM3_ESOFT) {
            DropField();
            PrintAndLogEx(ERR, "Error: tag didn't contain a NDEF Container");
            return PM3_ESOFT;
        }

        // max datasize;
        maxsize = ndef_get_maxsize(data + 12);
    }

    // iceman: maybe always take MIN of tag identified size vs NDEF reported size?
    // fix: UL_EV1 48bytes != NDEF reported size
    for (uint8_t i = 0; i < ARRAYLEN(UL_TYPES_ARRAY); i++) {
        if (tagtype & UL_TYPES_ARRAY[i]) {

            if (maxsize != (UL_MEMORY_ARRAY[i] * 4)) {
                PrintAndLogEx(INFO, "Tag reported size vs NDEF reported size mismatch. Using smallest value");
            }
            maxsize = MIN(maxsize, (UL_MEMORY_ARRAY[i] * 4));
            break;
        }
    }

    // allocate mem
    uint8_t *records = calloc(maxsize, sizeof(uint8_t));
    if (records == NULL) {
        DropField();
        return PM3_EMALLOC;
    }

    // read NDEF records.
    for (uint16_t i = 0, j = 0; i < maxsize; i += 16, j += 4) {
        status = ul_read(4 + j, records + i, 16);
        if (status == -1) {
            DropField();
            PrintAndLogEx(ERR, "Error: tag didn't answer to READ");
            free(records);
            return PM3_ESOFT;
        }
    }

    DropField();
    status = NDEFDecodeAndPrint(records, (size_t)maxsize, true);
    free(records);
    return status;
}
//------------------------------------
// Menu Stuff
//------------------------------------
static command_t CommandTable[] = {
    {"help",    CmdHelp,                   AlwaysAvailable, "This help"},
    {"info",    CmdHF14AMfUInfo,           IfPm3Iso14443a,  "Tag information"},
    {"dump",    CmdHF14AMfUDump,           IfPm3Iso14443a,  "Dump Ultralight / Ultralight-C / NTAG tag to binary file"},
    {"restore", CmdHF14AMfURestore,        IfPm3Iso14443a,  "Restore a dump onto a MFU MAGIC tag"},
    {"eload",   CmdHF14AMfUeLoad,          IfPm3Iso14443a,  "load Ultralight .eml dump file into emulator memory"},
    {"rdbl",    CmdHF14AMfURdBl,           IfPm3Iso14443a,  "Read block"},
    {"wrbl",    CmdHF14AMfUWrBl,           IfPm3Iso14443a,  "Write block"},
    {"cauth",   CmdHF14AMfUCAuth,          IfPm3Iso14443a,  "Authentication    - Ultralight C"},
    {"setpwd",  CmdHF14AMfUCSetPwd,        IfPm3Iso14443a,  "Set 3des password - Ultralight-C"},
    {"setuid",  CmdHF14AMfUCSetUid,        IfPm3Iso14443a,  "Set UID - MAGIC tags only"},
    {"sim",     CmdHF14AMfUSim,            IfPm3Iso14443a,  "Simulate Ultralight from emulator memory"},
    {"gen",     CmdHF14AMfUGenDiverseKeys, AlwaysAvailable, "Generate 3des mifare diversified keys"},
    {"pwdgen",  CmdHF14AMfUPwdGen,         AlwaysAvailable, "Generate pwd from known algos"},
    {"otptear", CmdHF14AMfuOtpTearoff,     IfPm3Iso14443a,  "Tear-off test on OTP bits"},
//    {"countertear", CmdHF14AMfuEv1CounterTearoff,     IfPm3Iso14443a,  "Tear-off test on Ev1 Counter bits"},
    {"ndef",    CmdHF14MfuNDEF,            IfPm3Iso14443a,  "Prints NDEF records from card"},
    {NULL, NULL, NULL, NULL}
};

static int CmdHelp(const char *Cmd) {
    (void)Cmd; // Cmd is not used so far
    CmdsHelp(CommandTable);
    return PM3_SUCCESS;
}

int CmdHFMFUltra(const char *Cmd) {
    clearCommandBuffer();
    return CmdsParse(CommandTable, Cmd);
}
