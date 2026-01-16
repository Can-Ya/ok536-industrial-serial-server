#include "modbus_core.h"

int main()
{
    uint8_t test_data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = modbus_crc16(test_data, sizeof(test_data));
    printf("Test CRC16: 0x%04X (expected: 0x840A)\n", crc);

    ModbusParser parser;
    modbus_parser_init(&parser, 100);
    ModbusRTUFrame rtu_frame;
    memset(&rtu_frame, 0, sizeof(ModbusRTUFrame)); // 初始化避免脏数据
    uint8_t rtu_data[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x0A, 0x84}; // CRC低字节0x0A，高字节0x84（符合Modbus RTU标准）
    int ret = -1;
    for (int i = 0; i < sizeof(rtu_data); i++) {
        ret = modbus_parse_rtu_byte(&parser, rtu_data[i], &rtu_frame);
        printf("Parse byte %d (0x%02X), ret=%d\n", i, rtu_data[i], ret);
    }

    if (ret == 0) {
        printf("RTU parse success:\n");
        printf("  Slave addr: 0x%02X\n", rtu_frame.slave_addr);
        printf("  Func code: 0x%02X\n", rtu_frame.func_code);
        printf("  Data len: %d\n", rtu_frame.data_len);
        printf("  CRC: 0x%04X\n", rtu_frame.crc);

        ModbusTCPFrame tcp_frame;
        ret = modbus_rtu_to_tcp(&rtu_frame, 0x0001, &tcp_frame);
        if (ret == 0) {
            printf("RTU to TCP success:\n");
            printf("  Transaction ID: 0x%04X\n", tcp_frame.transaction_id);
            printf("  Protocol ID: 0x%04X\n", tcp_frame.protocol_id);
            printf("  Length: %d\n", tcp_frame.length);
            printf("  Slave addr: 0x%02X\n", tcp_frame.slave_addr);
            printf("  Func code: 0x%02X\n", tcp_frame.func_code);
        }

        ModbusRTUFrame rtu_frame2;
        memset(&rtu_frame2, 0, sizeof(ModbusRTUFrame));
        ret = modbus_tcp_to_rtu(&tcp_frame, &rtu_frame2);
        if (ret == 0) {
            printf("TCP to RTU success, CRC: 0x%04X (expected: 0x840A)\n", rtu_frame2.crc);
        }
    } else {
        printf("RTU parse failed, ret=%d\n", ret);
        memset(&rtu_frame, 0, sizeof(ModbusRTUFrame));
    }

    return 0;
}