/* SwitchDoc Labs WeatherSense Wireless AQI
 * Uses:  RadioHead ASK (generic) protocol
 *
 * Default transmitter speed is 2000 bits per second, i.e. 500 us per bit.
 * The symbol encoding ensures a maximum run (gap) of 4x bit-width.
 * Sensible Living uses a speed of 1000, i.e. 1000 us per bit.
 */

#include "decoder.h"

// Maximum message length (including the headers, byte count and FCS) we are willing to support
// This is pretty arbitrary
#define RH_ASK_MAX_PAYLOAD_LEN 67
#define RH_ASK_HEADER_LEN      4
#define RH_ASK_MAX_MESSAGE_LEN (RH_ASK_MAX_PAYLOAD_LEN - RH_ASK_HEADER_LEN - 3)

uint8_t switchdoclabs_weathersenseAQI_payload[RH_ASK_MAX_PAYLOAD_LEN] = {0};
int switchdoclabs_weathersenseAQI_data_payload[RH_ASK_MAX_MESSAGE_LEN];

// Note: all the "4to6 code" came from RadioHead source code.
// see: http://www.airspayce.com/mikem/arduino/RadioHead/index.html

// 4 bit to 6 bit symbol converter table
// Used to convert the high and low nybbles of the transmitted data
// into 6 bit symbols for transmission. Each 6-bit symbol has 3 1s and 3 0s
// with at most 3 consecutive identical bits.
// Concatenated symbols have runs of at most 4 identical bits.
static uint8_t symbols[] = {
        0x0d, 0x0e, 0x13, 0x15, 0x16, 0x19, 0x1a, 0x1c,
        0x23, 0x25, 0x26, 0x29, 0x2a, 0x2c, 0x32, 0x34};

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

static int switchdoclabs_weathersenseAQI_ask_extract(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t row, /*OUT*/ uint8_t *payload)
{
    int len     = bitbuffer->bits_per_row[row];
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
        uint8_t byte      = hi_nibble << 4 | lo_nibble;
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
    crc           = (payload[msg_len - 1] << 8) | payload[msg_len - 2];
    crc_recompute = ~crc16lsb(payload, msg_len - 2, 0x8408, 0xFFFF);
    if (crc_recompute != crc) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: CRC error: %04X != %04X\n", __func__, crc_recompute, crc);
        }
        return DECODE_FAIL_MIC;
    }

    return msg_len;
}

static long AQIconvertByteToLong(uint8_t buffer[], int index)
{

    union Long {
        struct {
            uint8_t byte1;
            uint8_t byte2;
            uint8_t byte3;
            uint8_t byte4;
        };
        long word;
    };

    union Long sdlData;

    sdlData.byte1 = buffer[index];
    sdlData.byte2 = buffer[index + 1];
    sdlData.byte3 = buffer[index + 2];
    sdlData.byte4 = buffer[index + 3];
    return sdlData.word;
}

static unsigned int AQIconvertByteToUnsignedInt(uint8_t buffer[], int index)
{

    union sdlInt {
        struct {
            uint8_t byte1;
            uint8_t byte2;
        };
        unsigned int word;
    };

    union sdlInt sdlData;

    sdlData.byte1 = buffer[index];
    sdlData.byte2 = buffer[index + 1];
    return sdlData.word;
}

static float AQIconvertByteToFloat(uint8_t buffer[], int index)
{

    union Float {
        struct {
            uint8_t byte1;
            uint8_t byte2;
            uint8_t byte3;
            uint8_t byte4;
        };
        float word;
    };

    union Float sdlData;

    sdlData.byte1 = buffer[index];
    sdlData.byte2 = buffer[index + 1];
    sdlData.byte3 = buffer[index + 2];
    sdlData.byte4 = buffer[index + 3];

    return sdlData.word;
}

// AQI Calculation

#define AMOUNT_OF_LEVELS 6

static int get_grid_index_(uint16_t value, int array[AMOUNT_OF_LEVELS][2])
{
    for (int i = 0; i < AMOUNT_OF_LEVELS; i++) {
        if (value >= array[i][0] && value <= array[i][1]) {
            return i;
        }
    }
    return -1;
}

static int index_grid_[AMOUNT_OF_LEVELS][2] = {{0, 51}, {51, 100}, {101, 150}, {151, 200}, {201, 300}, {301, 500}};

static int pm2_5_calculation_grid_[AMOUNT_OF_LEVELS][2] = {{0, 12}, {13, 35}, {36, 55}, {56, 150}, {151, 250}, {251, 500}};

static int pm10_0_calculation_grid_[AMOUNT_OF_LEVELS][2] = {{0, 54}, {55, 154}, {155, 254},
        {255, 354}, {355, 424}, {425, 604}};

static int calculate_index_(uint16_t value, int array[AMOUNT_OF_LEVELS][2])
{
    int grid_index = get_grid_index_(value, array);
    int aqi_lo     = index_grid_[grid_index][0];
    int aqi_hi     = index_grid_[grid_index][1];
    int conc_lo    = array[grid_index][0];
    int conc_hi    = array[grid_index][1];

    return ((aqi_hi - aqi_lo) / (conc_hi - conc_lo)) * (value - conc_lo) + aqi_lo;
}

 static unsigned int get_aqi(unsigned int pm2_5_value, unsigned int pm10_0_value)
{
    // from esphome

    int pm2_5_index  = calculate_index_(pm2_5_value, pm2_5_calculation_grid_);
    int pm10_0_index = calculate_index_(pm10_0_value, pm10_0_calculation_grid_);

    return (pm2_5_index < pm10_0_index) ? pm10_0_index : pm2_5_index;
}

static int switchdoclabs_weathersenseAQI_ask_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t row = 0; // we are considering only first row
    int msg_len, data_len, header_to, header_from, header_id, header_flags;

    // protocol data
    long messageID;
    uint8_t SolarMAXID;
    uint8_t ProtocolVersion;
    uint8_t SoftwareVersion;
    uint8_t WeatherSenseProtocol;

    unsigned int PM1_0S;
    unsigned int PM2_5S;
    unsigned int PM10S;
    unsigned int PM1_0A;
    unsigned int PM2_5A;
    unsigned int PM10A;
    unsigned int EPAAQI;

    float BatteryVoltage;
    float BatteryCurrent;
    float LoadVoltage;
    float LoadCurrent;
    float SolarPanelVoltage;
    float SolarPanelCurrent;
    unsigned long AuxA;
    //float AuxB;

    msg_len = switchdoclabs_weathersenseAQI_ask_extract(decoder, bitbuffer, row, switchdoclabs_weathersenseAQI_payload);
    if (msg_len <= 0) {
        return msg_len; // pass error code on
    }
    data_len = msg_len - RH_ASK_HEADER_LEN - 3;

    header_to    = switchdoclabs_weathersenseAQI_payload[1];
    header_from  = switchdoclabs_weathersenseAQI_payload[2];
    header_id    = switchdoclabs_weathersenseAQI_payload[3];
    header_flags = switchdoclabs_weathersenseAQI_payload[4];

    // gather data
    messageID = AQIconvertByteToLong(switchdoclabs_weathersenseAQI_payload, 5);

    SolarMAXID           = switchdoclabs_weathersenseAQI_payload[9];
    WeatherSenseProtocol = switchdoclabs_weathersenseAQI_payload[10];
    if (decoder->verbose > 1) {
        fprintf(stderr, "%d: WeatherSenseProtocol\n", WeatherSenseProtocol);
    }

    if (WeatherSenseProtocol != 15) {
        // only accept weathersenseAQI protocols
        return 0;
    }

    ProtocolVersion = switchdoclabs_weathersenseAQI_payload[11];

    PM1_0S = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 12);
    PM2_5S = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 14);
    PM10S  = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 16);
    PM1_0A = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 18);
    PM2_5A = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 20);
    PM10A  = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 22);
    EPAAQI = AQIconvertByteToUnsignedInt(switchdoclabs_weathersenseAQI_payload, 24);

    LoadVoltage       = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 26);
    BatteryVoltage    = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 30);
    BatteryCurrent    = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 34);
    LoadCurrent       = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 38);
    SolarPanelVoltage = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 42);
    SolarPanelCurrent = AQIconvertByteToFloat(switchdoclabs_weathersenseAQI_payload, 46);
    AuxA              = switchdoclabs_weathersenseAQI_payload[50] & 0x0F;
    SoftwareVersion   = (switchdoclabs_weathersenseAQI_payload[50] & 0xF0) >> 4;
    // Now calculate EPA AQI

    // EPAAQI = get_aqi(PM2_5S,PM10S);

    // Format data
    for (int j = 0; j < msg_len; j++) {
        switchdoclabs_weathersenseAQI_data_payload[j] = (int)switchdoclabs_weathersenseAQI_payload[5 + j];
    }

    // now build output
    data = data_make(
            "model", "", DATA_STRING, "SwitchDoc Labs WeatherSense Wireless AQI", "SwitchDoc Labs AQI",
            "len", "Data len", DATA_INT, data_len,

            "messageid", "Message ID", DATA_INT, messageID,
            "deviceid", "Device ID", DATA_INT, SolarMAXID,
            "protocolversion", "Protocol Version", DATA_INT, ProtocolVersion,
            "softwareversion", "Software Version", DATA_INT, SoftwareVersion,
            "weathersenseprotocol", "WeatherSense Type", DATA_INT, WeatherSenseProtocol,
            "PM1.0S", "PM1.0 Standard(ug/m)", DATA_INT, PM1_0S,
            "PM2.5S", "PM2.5 Standard(ug/m)", DATA_INT, PM2_5S,
            "PM10S", "PM10 Standard(ug/m)", DATA_INT, PM10S,
            "PM1.0A", "PM1.0 Atmospheric(ug/m)", DATA_INT, PM1_0A,
            "PM2.5A", "PM2.5 Atmospheric(ug/m)", DATA_INT, PM2_5A,
            "PM10A", "PM10 Atmospheric(ug/m)", DATA_INT, PM10A,
            "AQI", "AQI EPA", DATA_INT, EPAAQI,
            "loadvoltage", "Load Voltage", DATA_DOUBLE, LoadVoltage,
            "batteryvoltage", "Battery Voltage", DATA_DOUBLE, BatteryVoltage,
            "batterycurrent", "Battery Current", DATA_DOUBLE, BatteryCurrent,
            "loadcurrent", "Load Current", DATA_DOUBLE, LoadCurrent,
            "solarpanelvoltage", "Solar Panel Voltage", DATA_DOUBLE, SolarPanelVoltage,
            "solarpanelcurrent", "Solar Panel Current", DATA_DOUBLE, SolarPanelCurrent,
            "auxa", "Aux A", DATA_INT, AuxA,

            "mic", "Integrity", DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *switchdoclabs_weathersenseAQI_ask_output_fields[] = {
        "model",
        "len",
        "sparebyte",
        "messageid",
        "weathersenseAQIid",
        "weathersenseAQIprotocol",
        "weathersenseAQIsoftwareversion",
        "weathersenseAQItype",
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
        NULL};



r_device switchdoclabs_weathersenseAQI = {
        .name        = "SwitchDoc Labs WeatherSenseAQI",
        .modulation  = OOK_PULSE_RZ,
        .short_width = 500,
        .long_width  = 500,
        .reset_limit = 5 * 500,
        .decode_fn   = &switchdoclabs_weathersenseAQI_ask_callback,
        .fields      = switchdoclabs_weathersenseAQI_ask_output_fields,
};
