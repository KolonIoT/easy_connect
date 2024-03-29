#ifndef MBED_EXTENDED_TESTS
    #error [NOT_SUPPORTED] Pressure tests are not supported by default
#endif

#include "mbed.h"
#include "ESP8266Interface.h"
#include "TCPSocket.h"
#include "greentea-client/test_env.h"
#include "unity/unity.h"
#include "utest.h"

using namespace utest::v1;


#ifndef MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN
#define MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN 64
#endif

#ifndef MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MAX
#define MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MAX 0x80000
#endif

#ifndef MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_SEED
#define MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_SEED 0x6d626564
#endif

#ifndef MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_DEBUG
#define MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_DEBUG false
#endif

#ifndef MBED_CFG_ESP8266_TX
#define MBED_CFG_ESP8266_TX D1
#endif

#ifndef MBED_CFG_ESP8266_RX
#define MBED_CFG_ESP8266_RX D0
#endif

#ifndef MBED_CFG_ESP8266_DEBUG
#define MBED_CFG_ESP8266_DEBUG false
#endif

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x


// Simple xorshift pseudorandom number generator
class RandSeq {
private:
    uint32_t x;
    uint32_t y;
    static const int A = 15;
    static const int B = 18;
    static const int C = 11;

public:
    RandSeq(uint32_t seed=MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_SEED)
        : x(seed), y(seed) {}

    uint32_t next(void) {
        x ^= x << A;
        x ^= x >> B;
        x ^= y ^ (y >> C);
        return x + y;
    }

    void skip(size_t size) {
        for (size_t i = 0; i < size; i++) {
            next();
        }
    }

    void buffer(uint8_t *buffer, size_t size) {
        RandSeq lookahead = *this;

        for (size_t i = 0; i < size; i++) {
            buffer[i] = lookahead.next() & 0xff;
        }
    }

    int cmp(uint8_t *buffer, size_t size) {
        RandSeq lookahead = *this;

        for (size_t i = 0; i < size; i++) {
            int diff = buffer[i] - (lookahead.next() & 0xff);
            if (diff != 0) {
                return diff;
            }
        }
        return 0;
    }
};

// Shared buffer for network transactions
uint8_t *buffer;
size_t buffer_size;

// Tries to get the biggest buffer possible on the device. Exponentially
// grows a buffer until heap runs out of space, and uses half to leave
// space for the rest of the program
void generate_buffer(uint8_t **buffer, size_t *size, size_t min, size_t max) {
    size_t i = min;
    while (i < max) {
        void *b = malloc(i);
        if (!b) {
            i /= 4;
            if (i < min) {
                i = min;
            }
            break;
        }
        free(b);
        i *= 2;
    }

    *buffer = (uint8_t *)malloc(i);
    *size = i;
    TEST_ASSERT(buffer);
}


void test_tcp_packet_pressure() {
    generate_buffer(&buffer, &buffer_size,
        MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN,
        MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MAX);
    printf("MBED: Generated buffer %d\r\n", buffer_size);

    ESP8266Interface net(MBED_CFG_ESP8266_TX, MBED_CFG_ESP8266_RX, MBED_CFG_ESP8266_DEBUG);
    int err = net.connect(STRINGIZE(MBED_CFG_ESP8266_SSID), STRINGIZE(MBED_CFG_ESP8266_PASS));
    TEST_ASSERT_EQUAL(0, err);

    printf("MBED: TCPClient IP address is '%s'\n", net.get_ip_address());
    printf("MBED: TCPClient waiting for server IP and port...\n");

    greentea_send_kv("target_ip", net.get_ip_address());

    char recv_key[] = "host_port";
    char ipbuf[60] = {0};
    char portbuf[16] = {0};
    unsigned int port = 0;

    greentea_send_kv("host_ip", " ");
    greentea_parse_kv(recv_key, ipbuf, sizeof(recv_key), sizeof(ipbuf));

    greentea_send_kv("host_port", " ");
    greentea_parse_kv(recv_key, portbuf, sizeof(recv_key), sizeof(ipbuf));
    sscanf(portbuf, "%u", &port);

    printf("MBED: Server IP address received: %s:%d \n", ipbuf, port);

    TCPSocket sock;
    SocketAddress tcp_addr(ipbuf, port);

    Timer timer;
    timer.start();

    // Tests exponentially growing sequences
    for (size_t size = MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN;
         size < MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MAX;
         size *= 2) {
        err = sock.open(&net);
        TEST_ASSERT_EQUAL(0, err);
        err = sock.connect(tcp_addr);
        TEST_ASSERT_EQUAL(0, err);
        printf("TCP: %s:%d streaming %d bytes\r\n", ipbuf, port, size);

        sock.set_blocking(false);

        // Loop to send/recv all data
        RandSeq tx_seq;
        RandSeq rx_seq;
        size_t rx_count = 0;
        size_t tx_count = 0;
        size_t window = buffer_size;

        while (tx_count < size || rx_count < size) {
            // Send out data
            if (tx_count < size) {
                size_t chunk_size = size - tx_count;
                if (chunk_size > window) {
                    chunk_size = window;
                }

                tx_seq.buffer(buffer, chunk_size);
                int td = sock.send(buffer, chunk_size);

                if (td > 0) {
                    if (MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_DEBUG) {
                        printf("TCP: tx -> %d\r\n", td);
                    }
                    tx_seq.skip(td);
                    tx_count += td;
                } else if (td != NSAPI_ERROR_WOULD_BLOCK) {
                    // We may fail to send because of buffering issues,
                    // cut buffer in half
                    if (window > MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN) {
                        window /= 2;
                    }

                    if (MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_DEBUG) {
                        printf("TCP: Not sent (%d), window = %d\r\n", td, window);
                    }
                }
            }

            // Verify recieved data
            while (rx_count < size) {
                int rd = sock.recv(buffer, buffer_size);
                TEST_ASSERT(rd > 0 || rd == NSAPI_ERROR_WOULD_BLOCK);
                if (rd > 0) {
                    if (MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_DEBUG) {
                        printf("TCP: rx <- %d\r\n", rd);
                    }
                    int diff = rx_seq.cmp(buffer, rd);
                    TEST_ASSERT_EQUAL(0, diff);
                    rx_seq.skip(rd);
                    rx_count += rd;
                } else if (rd == NSAPI_ERROR_WOULD_BLOCK) {
                    break;
                }
            }
        }

        err = sock.close();
        TEST_ASSERT_EQUAL(0, err);
    }

    timer.stop();
    printf("MBED: Time taken: %fs\r\n", timer.read());
    printf("MBED: Speed: %.3fkb/s\r\n",
            8*(2*MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MAX - 
            MBED_CFG_TCP_CLIENT_PACKET_PRESSURE_MIN) / (1000*timer.read()));

    net.disconnect();
}


// Test setup
utest::v1::status_t test_setup(const size_t number_of_cases) {
    GREENTEA_SETUP(120, "tcp_echo");
    return verbose_test_setup_handler(number_of_cases);
}

Case cases[] = {
    Case("TCP packet pressure", test_tcp_packet_pressure),
};

Specification specification(test_setup, cases);

int main() {
    return !Harness::run(specification);
}

