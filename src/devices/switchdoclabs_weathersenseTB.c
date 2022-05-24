/* SwitchDoc Labs WeatherSense Wireless ThunderBoard
 * Uses:  RadioHead ASK (generic) protocol
 *
 * Default transmitter speed is 2000 bits per second, i.e. 500 us per bit.
 * The symbol encoding ensures a maximum run (gap) of 4x bit-width.
 */

#include "decoder.h"

// Maximum message length (including the headers, byte count and FCS) we are willing to support
// This is pretty arbitrary
#define RH_ASK_MAX_PAYLOAD_LEN 67
#define RH_ASK_HEADER_LEN 4
#define RH_ASK_MAX_MESSAGE_LEN (RH_ASK_MAX_PAYLOAD_LEN - RH_ASK_HEADER_LEN - 3)

uint8_t switchdoclabs_weathersenseTB_payload[RH_ASK_MAX_PAYLOAD_LEN] = {0};
int switchdoclabs_weathersenseTB_data_payload[RH_ASK_MAX_MESSAGE_LEN];

// Note: all the "4to6 code" came from RadioHead source code.
// see: http://www.airspayce.com/mikem/arduino/RadioHead/index.html

// 4 bit to 6 bit symbol converter table
// Used to convert the high and low nybbles of the transmitted data
// into 6 bit symbols for transmission. Each 6-bit symbol has 3 1s and 3 0s
// with at most 3 consecutive identical bits.
// Concatenated symbols have runs of at most 4 identical bits.
static uint8_t symbols[] = {
        0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
        0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34
};

// Convert a 6 bit encoded symbol into its 4 bit decoded equivalent
static uint8_t symbol_6to4(uint8_t symbol)
{
    uint8_t i;
    // Linear search :-( Could have a 64 byte reverse lookup table?
    // There is a little speedup here courtesy Ralph Doncaster:
    // The shortcut works because bit 5 of the symbol is 1 for the last 8
    // symbols, and it is 0 for the first 8.
    // So we only have to search half the table
    for (i = (symbol >> 2) & 8; i < 16; i++) {
        if (symbol == symbols[i])
            return i;
    }
    return 0xFF; // Not found
}

static int switchdoclabs_weathersenseTB_ask_extract(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t row, /*OUT*/ uint8_t *payload)
{
    int len = bitbuffer->bits_per_row[row];
    int msg_len = RH_ASK_MAX_MESSAGE_LEN;
    int pos, nb_bytes;
    uint8_t rxBits[2] = {0};

    uint16_t crc, crc_recompute;

    // Looking for preamble
    uint8_t init_pattern[] = {
            0x55, // 8
            0x55, // 16
            0x55, // 24
            0x51, // 32
            0xcd, // 40
    };
    // The first 0 is ignored by the decoder, so we look only for 28 bits of "01"
    // and not 32. Also "0x1CD" is 0xb38 (RH_ASK_START_SYMBOL) with LSBit first.
    int init_pattern_len = 40;

    pos = bitbuffer_search(bitbuffer, row, 0, init_pattern, init_pattern_len);
    if (pos == len) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: preamble not found\n", __func__);
        }
        return DECODE_ABORT_EARLY;
    }

    // read "bytes" of 12 bit
    nb_bytes = 0;
    pos += init_pattern_len;
    for (; pos < len && nb_bytes < msg_len; pos += 12) {
        bitbuffer_extract_bytes(bitbuffer, row, pos, rxBits, /*len=*/16);
        // ^ we should read 16 bits and not 12, elsewhere last 4bits are ignored
        rxBits[0] = reverse8(rxBits[0]);
        rxBits[1] = reverse8(rxBits[1]);
        rxBits[1] = ((rxBits[1] & 0x0F) << 2) + (rxBits[0] >> 6);
        rxBits[0] &= 0x3F;
        uint8_t hi_nibble = symbol_6to4(rxBits[0]);
        if (hi_nibble > 0xF) {
            if (decoder->verbose) {
                fprintf(stderr, "%s: Error on 6to4 decoding high nibble: %X\n", __func__, rxBits[0]);
            }
            return DECODE_FAIL_SANITY;
        }
        uint8_t lo_nibble = symbol_6to4(rxBits[1]);
        if (lo_nibble > 0xF) {
            if (decoder->verbose) {
                fprintf(stderr, "%s: Error on 6to4 decoding low nibble: %X\n", __func__, rxBits[1]);
            }
            return DECODE_FAIL_SANITY;
        }
        uint8_t byte = hi_nibble << 4 | lo_nibble;
        payload[nb_bytes] = byte;
        if (nb_bytes == 0) {
            msg_len = byte;
        }
        nb_bytes++;
    }

    // Prevent buffer underflow when calculating CRC
    if (msg_len < 2) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: message too short to contain crc\n", __func__);
        }
        return DECODE_ABORT_LENGTH;
    }
    // Sanity check on excessive msg len
    if (msg_len > RH_ASK_MAX_MESSAGE_LEN) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: message too long: %d\n", __func__, msg_len);
        }
        return DECODE_ABORT_LENGTH;
    }

    // Check CRC
    crc = (payload[msg_len - 1] << 8) | payload[msg_len - 2];
    crc_recompute = ~crc16lsb(payload, msg_len - 2, 0x8408, 0xFFFF);
    if (crc_recompute != crc) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: CRC error: %04X != %04X\n", __func__, crc_recompute, crc);
        }
        return DECODE_FAIL_MIC;
    }

    return msg_len;
}

long TBconvertByteToLong(uint8_t buffer[], int index)
{

    
    union Long {
      struct{
        uint8_t   byte1;
        uint8_t   byte2;
        uint8_t   byte3;
        uint8_t   byte4;

      };
      long  word;
    
      };

      union Long myData;

      myData.byte1 = buffer[index];
      myData.byte2 = buffer[index+1];
      myData.byte3 = buffer[index+2];
      myData.byte4 = buffer[index+3];
      return myData.word;
    }

unsigned long TBconvertByteToUnsignedLong(uint8_t buffer[], int index)
{

    
    union Long {
      struct{
        uint8_t   byte1;
        uint8_t   byte2;
        uint8_t   byte3;
        uint8_t   byte4;

      };
      unsigned long  word;
    
      };

      union Long myData;

      myData.byte1 = buffer[index];
      myData.byte2 = buffer[index+1];
      myData.byte3 = buffer[index+2];
      myData.byte4 = buffer[index+3];
      return myData.word;
    }

unsigned int TBconvertByteToUnsignedInt(uint8_t buffer[], int index)
{

    
    union myInt {
      struct{
        uint8_t   byte1;
        uint8_t   byte2;

      };
      unsigned int  word;
    
      };

      union myInt myData;

      myData.byte1 = buffer[index];
      myData.byte2 = buffer[index+1];
      return myData.word;
    }

float TBconvertByteToFloat(uint8_t buffer[], int index)
{

    
    union Float {
      struct{
        uint8_t   byte1;
        uint8_t   byte2;
        uint8_t   byte3;
        uint8_t   byte4;

      };
      float  word;
    
      };

      union Float myData;

      myData.byte1 = buffer[index];
      myData.byte2 = buffer[index+1];
      myData.byte3 = buffer[index+2];
      myData.byte4 = buffer[index+3];

      return myData.word;
    }





static int switchdoclabs_weathersenseTB_ask_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t row = 0; // we are considering only first row
    int msg_len, data_len, header_to, header_from, header_id, header_flags;

// protocol data
    long messageID;
    uint8_t WSTBID;
    uint8_t ProtocolVersion;
    uint8_t SoftwareVersion;
    uint8_t WeatherSenseProtocol;

    uint8_t IRQSource;
    uint8_t PreviousInterruptResult;
    uint8_t LightningLastDistance;
    uint8_t SpareByte;

    unsigned long LightningCount;
    unsigned long InterruptCount;

    float BatteryVoltage;
    float BatteryCurrent;
    float LoadVoltage;
    float LoadCurrent;
    float SolarPanelVoltage;
    float SolarPanelCurrent;
    unsigned long AuxA;
    float AuxB;


    msg_len = switchdoclabs_weathersenseTB_ask_extract(decoder, bitbuffer, row, switchdoclabs_weathersenseTB_payload);
    if (msg_len <= 0) {
        return msg_len; // pass error code on
    }
    data_len = msg_len - RH_ASK_HEADER_LEN - 3;

    header_to = switchdoclabs_weathersenseTB_payload[1];
    header_from = switchdoclabs_weathersenseTB_payload[2];
    header_id = switchdoclabs_weathersenseTB_payload[3];
    header_flags = switchdoclabs_weathersenseTB_payload[4];


    // gather data
    messageID = TBconvertByteToLong(switchdoclabs_weathersenseTB_payload, 5);

    WSTBID = switchdoclabs_weathersenseTB_payload[9];
    WeatherSenseProtocol = switchdoclabs_weathersenseTB_payload[10];
    if (decoder->verbose > 1) {
        fprintf(stderr, "%d: WeatherSenseProtocol\n", WeatherSenseProtocol);
    }

    if (WeatherSenseProtocol != 16) 
    {
        // only accept weathersenseTB protocols
        return 0;
    }

    ProtocolVersion = switchdoclabs_weathersenseTB_payload[11];

    IRQSource = switchdoclabs_weathersenseTB_payload[12];
    PreviousInterruptResult = switchdoclabs_weathersenseTB_payload[13];
    LightningLastDistance = switchdoclabs_weathersenseTB_payload[14];
    SpareByte = switchdoclabs_weathersenseTB_payload[15];

    LightningCount = TBconvertByteToLong(switchdoclabs_weathersenseTB_payload, 16);
    InterruptCount = TBconvertByteToLong(switchdoclabs_weathersenseTB_payload, 20);
    
    LoadVoltage = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 24);
    BatteryVoltage = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 28);
    BatteryCurrent = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 32);
    LoadCurrent  = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 36);
    SolarPanelVoltage  = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 40);
    SolarPanelCurrent  = TBconvertByteToFloat(switchdoclabs_weathersenseTB_payload, 44);
    AuxA  = switchdoclabs_weathersenseTB_payload[48] & 0x0F;
    SoftwareVersion = (switchdoclabs_weathersenseTB_payload[48] & 0xF0)>>4;
    
    // Format data
    for (int j = 0; j < msg_len; j++) {
        switchdoclabs_weathersenseTB_data_payload[j] = (int)switchdoclabs_weathersenseTB_payload[5 + j];
    }


    // now build output
    data = data_make(
            "model",        "",             DATA_STRING, _X("SwitchDoc Labs WeatherSense Wireless ThunderBoard","SwitchDoc Labs TB"),
            "len",          "Data len",     DATA_INT, data_len,

            "messageid",        "Message ID",        DATA_INT, messageID,
            "deviceid",        "Device ID",        DATA_INT, WSTBID,
            "protocolversion",        "Protocol Version",   DATA_INT, ProtocolVersion,
            "softwareversion",        "Software Version",        DATA_INT, SoftwareVersion,
            "weathersenseprotocol",        "WeatherSense Type",        DATA_INT, WeatherSenseProtocol,
            "irqsource",        "Source of Interrupt",        DATA_INT, IRQSource,
            "previousinterruptresult",        "Previous Source of Interrupt",        DATA_INT, PreviousInterruptResult,
            "lightninglastdistance",        "Distance of Last Lightning",        DATA_INT, LightningLastDistance,
            "sparebyte",        "Spare Byte",        DATA_INT, SpareByte,
            "lightningcount",        "Lightning Count",        DATA_INT, LightningCount,
            "interruptcount",        "Interrupt Count ID",        DATA_INT, InterruptCount,

            "loadvoltage",        "Load Voltage",        DATA_DOUBLE, LoadVoltage,
            "batteryvoltage",        "Battery Voltage",        DATA_DOUBLE, BatteryVoltage,
            "batterycurrent",        "Battery Current",        DATA_DOUBLE, BatteryCurrent,
            "loadcurrent",        "Load Current",        DATA_DOUBLE, LoadCurrent,
            "solarpanelvoltage",        "Solar Panel Voltage",        DATA_DOUBLE, SolarPanelVoltage,
            "solarpanelcurrent",        "Solar Panel Current",        DATA_DOUBLE, SolarPanelCurrent,
            "auxa",        "Aux A",        DATA_INT, AuxA,

            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    
    decoder_output_data(decoder, data);

    return 1;
}


static char *switchdoclabs_weathersenseTB_ask_output_fields[] = {
    "model",
    "len",
    "messageid",
    "weathersenseTBid",
    "weathersenseTBprotocol",
    "weathersenseTBsoftwareversion",
    "weathersenseTBtype",
    
    "irqsource",
    "previousinterruptresult",
    "lightninglastdistance",
    "sparebyte",
    "lightningcount",
    "interruptcount",

    "loadvoltage",
    "insidetemperature",
    "insidehumidity",
    "batteryvoltage",
    "batterycurrent",
    "loadcurrent",
    "solarpanelvoltage",
    "solarpanelcurrent",
    "auxa",
    "mic",
    NULL
};


r_device switchdoclabs_weathersenseTB = {
    .name           = "SwitchDoc Labs WeatherSenseTB",
    .modulation     = OOK_PULSE_PCM_RZ,
    .short_width    = 500,
    .long_width     = 500,
    .reset_limit    = 5*500,
    .decode_fn      = &switchdoclabs_weathersenseTB_ask_callback,
    .fields         = switchdoclabs_weathersenseTB_ask_output_fields,
};

