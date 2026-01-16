#ifndef MODBUS_CORE_H
#define MODBUS_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MODBUS_MAX_FRAME_LEN 256
#define MODBUS_TCP_HEADER_LEN 6
#define MODBUS_CRC_LEN 2

// Modbus function codes
#define MODBUS_FC_READ_HOLDING_REGISTERS 0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER 0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS 0x10

// Modbus exception codes
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

// Modbus RTU frame structure
typedef struct {
    uint8_t slave_addr;          // Slave address (1-247)
    uint8_t func_code;           // Function code
    uint8_t data[MODBUS_MAX_FRAME_LEN - 3]; // Data field (exclude addr/func/crc)
    uint16_t data_len;           // Length of data field
    uint16_t crc;                // CRC16 checksum
} ModbusRTUFrame;

// Modbus TCP frame structure (MBAP header + PDU)
typedef struct {
    uint16_t transaction_id;     // Transaction ID (unique per request)
    uint16_t protocol_id;        // Protocol ID (0 for Modbus)
    uint16_t length;             // Length of remaining frame (slave + func + data)
    uint8_t slave_addr;          // Slave address
    uint8_t func_code;           // Function code
    uint8_t data[MODBUS_MAX_FRAME_LEN - MODBUS_TCP_HEADER_LEN - 1]; // Data field
    uint16_t data_len;           // Length of data field
} ModbusTCPFrame;

// Modbus parser state machine
typedef enum {
    MODBUS_STATE_IDLE,           // Idle state (wait for slave addr)
    MODBUS_STATE_SLAVE_ADDR,     // Received slave address
    MODBUS_STATE_FUNC_CODE,      // Received function code
    MODBUS_STATE_DATA,           // Receiving data field (unused in current logic)
    MODBUS_STATE_CRC1,           // Receiving CRC low byte
    MODBUS_STATE_CRC2,           // Receiving CRC high byte        
    MODBUS_STATE_COMPLETE        // Frame parse complete
} ModbusParseState;

// Modbus parser context
typedef struct {
    ModbusParseState state;      // Current parse state
    ModbusRTUFrame rtu_frame;    // Temp RTU frame buffer
    uint16_t data_idx;           // Current data field index
    uint32_t frame_timeout;      // Frame timeout (ms)
    uint64_t last_recv_time;     // Last byte receive time (ms)
} ModbusParser;

// Function declarations
uint16_t modbus_crc16(const uint8_t* data, uint16_t len);
void modbus_parser_init(ModbusParser* parser, uint32_t frame_timeout);
int modbus_parse_rtu_byte(ModbusParser* parser, uint8_t byte, ModbusRTUFrame* frame);
int modbus_rtu_to_tcp(const ModbusRTUFrame* rtu_frame, uint16_t transaction_id, ModbusTCPFrame* tcp_frame);
int modbus_tcp_to_rtu(const ModbusTCPFrame* tcp_frame, ModbusRTUFrame* rtu_frame);
int modbus_validate_func_code(uint8_t func_code);
int modbus_build_exception_rtu(uint8_t slave_addr, uint8_t func_code, uint8_t exception_code, ModbusRTUFrame* frame);

#endif // !MODBUS_CORE_H