/* SPWFSAxx Devices
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed_debug.h"

#include "SpwfSAInterface.h" /* must be included first */
#include "SPWFSAxx.h"

static const char out_delim[] = {SPWFSAxx::_cr_, '\0'};

SPWFSAxx::SPWFSAxx(PinName tx, PinName rx,
                   PinName rts, PinName cts,
                   SpwfSAInterface &ifce, bool debug,
                   PinName wakeup, PinName reset)
: _serial(tx, rx, SPWFXX_DEFAULT_BAUD_RATE), _parser(&_serial, out_delim),
  _wakeup(wakeup, 1), _reset(reset, 1),
  _rts(rts), _cts(cts),
  _timeout(SPWF_INIT_TIMEOUT), _dbg_on(debug),
  _pending_sockets_bitmap(0),
  _network_lost_flag(false),
  _associated_interface(ifce),
  _call_event_callback_blocked(false),
  _callback_func(),
  _packets(0), _packets_end(&_packets),
  _msg_buffer(ssid_buf)
{
    memset(_pending_pkt_sizes, 0, sizeof(_pending_pkt_sizes));

    _serial.set_baud(SPWFXX_DEFAULT_BAUD_RATE);
    _serial.sigio(Callback<void()>(this, &SPWFSAxx::_event_handler));
    _parser.debug_on(debug);

    _parser.oob("+WIND:55:Pending Data", callback(this, &SPWFSAxx::_packet_handler_th));
    _parser.oob("+WIND:58:Socket Closed", callback(this, &SPWFSAxx::_server_gone_handler));
    _parser.oob("+WIND:33:WiFi Network Lost", callback(this, &SPWFSAxx::_network_lost_handler_th));
    _parser.oob("+WIND:8:Hard Fault", callback(this, &SPWFSAxx::_hard_fault_handler));
    _parser.oob("+WIND:5:WiFi Hardware Failure", callback(this, &SPWFSAxx::_wifi_hwfault_handler));
    _parser.oob(SPWFXX_OOB_ERROR, callback(this, &SPWFSAxx::_error_handler));
}

bool SPWFSAxx::startup(int mode)
{
    /*Reset module*/
    if(!hw_reset()) {
        debug_if(_dbg_on, "\r\nSPWF> HW reset failed\r\n");
        return false;
    }

    /* factory reset */
    if(!(_parser.send(SPWFXX_SEND_FWCFG) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error restore factory default settings\r\n");
        return false;
    }

    /*switch off led*/
    if(!(_parser.send("AT+S.SCFG=blink_led,0") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error stop blinking led (%d)\r\n", __LINE__);
        return false;
    }

    /*set local echo to 0*/
    if(!(_parser.send(SPWFXX_SEND_DISABLE_LE) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error local echo set\r\n");
        return false;
    }

    /*set Wi-Fi mode and rate to b/g/n*/
    if(!(_parser.send("AT+S.SCFG=wifi_ht_mode,1") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error setting ht_mode\r\n");
        return false;
    }

    /*set the operational rate*/
    if(!(_parser.send("AT+S.SCFG=wifi_opr_rate_mask,0x003FFFCF") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error setting operational rates\r\n");
        return false;
    }

    /*set idle mode (0->idle, 1->STA,3->miniAP, 2->IBSS)*/
    if(!(_parser.send("AT+S.SCFG=wifi_mode,%d", mode) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error WiFi mode set idle (%d)\r\n", __LINE__);
        return false;
    }

#if defined(MBED_MAJOR_VERSION)
#if !DEVICE_SERIAL_FC || (MBED_VERSION < MBED_ENCODE_VERSION(5, 7, 0))
    /*disable HW flow control*/
    if(!(_parser.send(SPWFXX_SEND_DISABLE_FC) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error disabling HW flow control\r\n");
        return false;
    }
#else // DEVICE_SERIAL_FC && (MBED_VERSION >= MBED_ENCODE_VERSION(5, 7, 0))
    if((_rts != NC) && (_cts != NC)) {
        /*enable HW flow control*/
        if(!(_parser.send(SPWFXX_SEND_ENABLE_FC) && _recv_ok()))
        {
            debug_if(_dbg_on, "\r\nSPWF> error enabling HW flow control\r\n");
            return false;
        }

        /*configure pins for HW flow control*/
        _serial.set_flow_control(SerialBase::RTSCTS, _rts, _cts);
    } else {
        /*disable HW flow control*/
        if(!(_parser.send(SPWFXX_SEND_DISABLE_FC) && _recv_ok()))
        {
            debug_if(_dbg_on, "\r\nSPWF> error disabling HW flow control\r\n");
            return false;
        }
    }
#endif // DEVICE_SERIAL_FC && (MBED_VERSION >= MBED_ENCODE_VERSION(5, 7, 0))
#else // !defined(MBED_MAJOR_VERSION) - Assuming `master` branch
#if !DEVICE_SERIAL_FC
    /*disable HW flow control*/
    if(!(_parser.send(SPWFXX_SEND_DISABLE_FC) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error disabling HW flow control\r\n");
        return false;
    }
#else // DEVICE_SERIAL_FC
    if((_rts != NC) && (_cts != NC)) {
        /*enable HW flow control*/
        if(!(_parser.send(SPWFXX_SEND_ENABLE_FC) && _recv_ok()))
        {
            debug_if(_dbg_on, "\r\nSPWF> error enabling HW flow control\r\n");
            return false;
        }

        /*configure pins for HW flow control*/
        _serial.set_flow_control(SerialBase::RTSCTS, _rts, _cts);
    } else {
        /*disable HW flow control*/
        if(!(_parser.send(SPWFXX_SEND_DISABLE_FC) && _recv_ok()))
        {
            debug_if(_dbg_on, "\r\nSPWF> error disabling HW flow control\r\n");
            return false;
        }
    }
#endif // DEVICE_SERIAL_FC
#endif // !defined(MBED_MAJOR_VERSION)

    /* Disable selected WINDs */
    _winds_on();

    /* sw reset */
    if(!reset()) {
        debug_if(_dbg_on, "\r\nSPWF> SW reset failed (%s, %d)\r\n", __func__, __LINE__);
        return false;
    }

#ifndef NDEBUG
    if (!(_parser.send(SPWFXX_SEND_GET_CONS_STATE)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting console state\r\n");
        return false;
    }

    if (!(_parser.send(SPWFXX_SEND_GET_CONS_SPEED)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting console speed\r\n");
        return false;
    }

    if (!(_parser.send(SPWFXX_SEND_GET_HWFC_STATE)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting hwfc state\r\n");
        return false;
    }

#if (MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1) || defined(IDW01M1_FW_REL_35X)
    /* betzw: IDW01M1 FW versions <3.5 seem to have problems with the following two commands.
     *        For the sake of simplicity, just excluding them for IDW01M1 in general.
     */
    if (!(_parser.send(SPWFXX_SEND_GET_CONS_DELIM)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting console delimiter\r\n");
        return false;
    }

    if (!(_parser.send(SPWFXX_SEND_GET_CONS_ERRS)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting console error setting\r\n");
        return false;
    }
#endif // (MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1) || defined(IDW01M1_FW_REL_35X)

    if (!(_parser.send("AT+S.GCFG=sleep_enabled")
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting sleep state enabled\r\n");
        return false;
    }

    if (!(_parser.send("AT+S.GCFG=wifi_powersave")
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting powersave mode\r\n");
        return false;
    }

    if (!(_parser.send("AT+S.GCFG=standby_enabled")
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> error getting standby state enabled\r\n");
        return false;
    }
#endif

    return true;
}

bool SPWFSAxx::_wait_console_active(void) {
    int trials = 0;

    while(true) {
        if (_parser.recv("+WIND:0:Console active\n") && _recv_delim_lf()) {
            debug_if(_dbg_on, "AT^ +WIND:0:Console active\r\n");
            return true;
        }
        if(++trials >= SPWFXX_MAX_TRIALS) {
            debug("%s (%d) - ERROR: Should never happen!\r\n", __func__, __LINE__);
            empty_rx_buffer();
            return false;
        }
    }
}

bool SPWFSAxx::_wait_wifi_hw_started(void) {
    int trials = 0;

    while(true) {
        if (_parser.recv("+WIND:32:WiFi Hardware Started\n") && _recv_delim_lf()) {
            debug_if(_dbg_on, "AT^ +WIND:32:WiFi Hardware Started\r\n");
            return true;
        }
        if(++trials >= SPWFXX_MAX_TRIALS) {
            debug("%s (%d) - ERROR: Should never happen!\r\n", __func__, __LINE__);
            empty_rx_buffer();
            return false;
        }
    }
}

bool SPWFSAxx::hw_reset(void)
{
#if (MBED_CONF_IDW0XX1_EXPANSION_BOARD != IDW04A1) || !defined(IDW04A1_WIFI_HW_BUG_WA) // betzw: HW reset doesn't work as expected on unmodified X_NUCLEO_IDW04A1 expansion boards
    _reset.write(0);
    wait_ms(200);
    _reset.write(1); 
#else // (MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1) && defined(IDW04A1_WIFI_HW_BUG_WA): substitute with SW reset
    _parser.send(SPWFXX_SEND_SW_RESET);
#endif // (MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1) && defined(IDW04A1_WIFI_HW_BUG_WA)
    return _wait_console_active();
}

bool SPWFSAxx::reset(void)
{
    bool ret;

    /* save current setting in flash */
    if(!(_parser.send(SPWFXX_SEND_SAVE_SETTINGS) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error saving configuration to flash\r\n");
        return false;
    }

    if(!_parser.send(SPWFXX_SEND_SW_RESET)) return false; /* betzw - NOTE: "keep the current state and reset the device".
                                                                     We assume that the module informs us about the
                                                                     eventual closing of sockets via "WIND" asynchronous
                                                                     indications! So everything regarding the clean-up
                                                                     of these situations is handled there. */
    ret = _wait_wifi_hw_started();

    return ret;
}

/* Security Mode
   None          = 0, 
   WEP           = 1,
   WPA_Personal  = 2,
 */
bool SPWFSAxx::connect(const char *ap, const char *passPhrase, int securityMode)
{
    int trials;

    //AT+S.SCFG=wifi_wpa_psk_text,%s
    if(!(_parser.send("AT+S.SCFG=wifi_wpa_psk_text,%s", passPhrase) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error pass set\r\n");
        return false;
    } 

    //AT+S.SSIDTXT=%s
    if(!(_parser.send("AT+S.SSIDTXT=%s", ap) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error ssid set\r\n");
        return false;
    }

    //AT+S.SCFG=wifi_priv_mode,%d
    if(!(_parser.send("AT+S.SCFG=wifi_priv_mode,%d", securityMode) && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error security mode set\r\n");
        return false;
    }

    /*set STA mode (0->idle, 1->STA,3->miniAP, 2->IBSS)*/
    if(!(_parser.send("AT+S.SCFG=wifi_mode,1") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error WiFi mode set 1 (STA)\r\n");
        return false;
    }

    /* sw reset */
    if(!reset()) {
        debug_if(_dbg_on, "\r\nSPWF> SW reset failed (%s, %d)\r\n", __func__, __LINE__);
        return false;
    }

    trials = 0;
    while(true) {
        if(_parser.recv("%[^\n]\n", _msg_buffer) && _recv_delim_lf())
        {
            if(strstr(_msg_buffer, ":24:") != NULL) { // WiFi Up
                debug_if(_dbg_on, "AT^ %s\n", _msg_buffer);
                if(strchr(_msg_buffer, '.') != NULL) { // IPv4 address
                    break;
                } else {
                    continue;
                }
            }
            if(strstr(_msg_buffer, ":40:") != NULL) { // Deauthentication
                debug_if(_dbg_on, "AT~ %s\n", _msg_buffer);
                if(++trials < 3) { // give it three trials
                    continue;
                }
                empty_rx_buffer();
                disconnect();
                return false;
            } else {
                debug_if(_dbg_on, "AT] %s\n", _msg_buffer);
            }
            continue;
        }
        if(++trials >= SPWFXX_MAX_TRIALS) {
            debug("%s (%d) - ERROR: Should never happen!\r\n", __func__, __LINE__);
            empty_rx_buffer();
            return false;
        }
    }

    return true;
}

bool SPWFSAxx::disconnect(void)
{
#if MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1
    /*disable Wi-Fi device*/
    if(!(_parser.send("AT+S.WIFI=0") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error disabling WiFi\r\n");
        return false;
    }
#endif // IDW04A1

    /*set idle mode (0->idle, 1->STA,3->miniAP, 2->IBSS)*/
    if(!(_parser.send("AT+S.SCFG=wifi_mode,0") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error WiFi mode set idle (%d)\r\n", __LINE__);
        return false;
    }

#if MBED_CONF_IDW0XX1_EXPANSION_BOARD == IDW04A1
    /*enable Wi-Fi device*/
    if(!(_parser.send("AT+S.WIFI=1") && _recv_ok()))
    {
        debug_if(_dbg_on, "\r\nSPWF> error enabling WiFi\r\n");
        return false;
    }
#endif // IDW04A1

    // reset module
    if(!reset()) {
        debug_if(_dbg_on, "\r\nSPWF> SW reset failed (%s, %d)\r\n", __func__, __LINE__);
        return false;
    }

    return true;
}

const char *SPWFSAxx::getIPAddress(void)
{
    unsigned int n1, n2, n3, n4;

    if (!(_parser.send("AT+S.STS=ip_ipaddr")
            && _parser.recv(SPWFXX_RECV_IP_ADDR, &n1, &n2, &n3, &n4)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> get IP address error\r\n");
        return NULL;
    }

    debug_if(_dbg_on, "AT^ ip_ipaddr = %u.%u.%u.%u\r\n", n1, n2, n3, n4);

    sprintf((char*)_ip_buffer,"%u.%u.%u.%u", n1, n2, n3, n4);
    return _ip_buffer;
}

const char *SPWFSAxx::getGateway(void)
{
    unsigned int n1, n2, n3, n4;

    if (!(_parser.send("AT+S.STS=ip_gw")
            && _parser.recv(SPWFXX_RECV_GATEWAY, &n1, &n2, &n3, &n4)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> get gateway error\r\n");
        return NULL;
    }

    debug_if(_dbg_on, "AT^ ip_gw = %u.%u.%u.%u\r\n", n1, n2, n3, n4);

    sprintf((char*)_gateway_buffer,"%u.%u.%u.%u", n1, n2, n3, n4);
    return _gateway_buffer;
}

const char *SPWFSAxx::getNetmask(void)
{
    unsigned int n1, n2, n3, n4;

    if (!(_parser.send("AT+S.STS=ip_netmask")
            && _parser.recv(SPWFXX_RECV_NETMASK, &n1, &n2, &n3, &n4)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> get netmask error\r\n");
        return NULL;
    }

    debug_if(_dbg_on, "AT^ ip_netmask = %u.%u.%u.%u\r\n", n1, n2, n3, n4);

    sprintf((char*)_netmask_buffer,"%u.%u.%u.%u", n1, n2, n3, n4);
    return _netmask_buffer;
}

int8_t SPWFSAxx::getRssi(void)
{
    int ret;

    if (!(_parser.send("AT+S.PEERS=0,rx_rssi")
            && _parser.recv(SPWFXX_RECV_RX_RSSI, &ret)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> get RX rssi error\r\n");
        return 0;
    }

    return (int8_t)ret;
}

const char *SPWFSAxx::getMACAddress(void)
{
    unsigned int n1, n2, n3, n4, n5, n6;

    if (!(_parser.send("AT+S.GCFG=nv_wifi_macaddr")
            && _parser.recv(SPWFXX_RECV_MAC_ADDR, &n1, &n2, &n3, &n4, &n5, &n6)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> get MAC address error\r\n");
        return 0;
    }

    debug_if(_dbg_on, "AT^ nv_wifi_macaddr = %x:%x:%x:%x:%x:%x\r\n", n1, n2, n3, n4, n5, n6);

    sprintf((char*)_mac_buffer,"%02X:%02X:%02X:%02X:%02X:%02X", n1, n2, n3, n4, n5, n6);
    return _mac_buffer;
}

bool SPWFSAxx::isConnected(void)
{
    return _associated_interface._connected_to_network;
}

bool SPWFSAxx::send(int spwf_id, const void *data, uint32_t amount)
{
    uint32_t sent = 0U, to_send;
    bool ret = true;

    for(to_send = (amount > SPWFSA_SEND_PKTSIZE) ? SPWFSA_SEND_PKTSIZE : amount;
            sent < amount;
            to_send = ((amount - sent) > SPWFSA_SEND_PKTSIZE) ? SPWFSA_SEND_PKTSIZE : (amount - sent)) {
        {
            BH_HANDLER;

            if (!(_parser.send("AT+S.SOCKW=%d,%d", spwf_id, (unsigned int)to_send)
                    && (_parser.write(((char*)data)+sent, (int)to_send) == (int)to_send)
                    && _recv_ok())) {
                // betzw - TODO: handle different errors more accurately!
                ret = false;
                break;
            }
        }

        sent += to_send;
    }

    return ret;
}

int SPWFSAxx::_read_len(int spwf_id) {
    unsigned int amount;

    if (!(_parser.send("AT+S.SOCKQ=%d", spwf_id)
            && _parser.recv(SPWFXX_RECV_DATALEN, &amount)
            && _recv_ok())) {
        debug_if(_dbg_on, "\r\nSPWF> %s failed\r\n", __func__);
        return SPWFXX_ERR_LEN;
    }

    if(amount > 0) {
        debug_if(_dbg_on, "%s():\t\t%d:%d\r\n", __func__, spwf_id, amount);
    }

    MBED_ASSERT(((int)amount) >= 0);

    return (int)amount;
}

#define SPWFXX_WINDS_OFF "0xFFFFFFFF"

void SPWFSAxx::_winds_on(void) {
    if(!(_parser.send(SPWFXX_SEND_WIND_OFF_HIGH SPWFXX_WINDS_HIGH_ON) && _recv_ok())) {
    }
    if(!(_parser.send(SPWFXX_SEND_WIND_OFF_MEDIUM SPWFXX_WINDS_MEDIUM_ON) && _recv_ok())) {
    }
    if(!(_parser.send(SPWFXX_SEND_WIND_OFF_LOW SPWFXX_WINDS_LOW_ON) && _recv_ok())) {
    }
}

/* Note: in case of error blocking has been (tried to be) lifted */
bool SPWFSAxx::_winds_off(void) {
    if (!(_parser.send(SPWFXX_SEND_WIND_OFF_LOW SPWFXX_WINDS_OFF)
            && _recv_ok())) {
        _winds_on();
        return false;
    }

    if (!(_parser.send(SPWFXX_SEND_WIND_OFF_MEDIUM SPWFXX_WINDS_OFF)
            && _recv_ok())) {
        _winds_on();
        return false;
    }

    if (!(_parser.send(SPWFXX_SEND_WIND_OFF_HIGH SPWFXX_WINDS_OFF)
            && _recv_ok())) {
        _winds_on();
        return false;
    }

    return true;
}

void SPWFSAxx::_execute_bottom_halves(void) {
    _network_lost_handler_bh();
    _packet_handler_bh();
}

void SPWFSAxx::_read_in_pending(void) {
    static int internal_id_cnt = 0;

    while(_is_data_pending()) {
        if(_associated_interface._socket_has_connected(internal_id_cnt)) {
            int spwf_id = _associated_interface._ids[internal_id_cnt].spwf_id;

            if(_is_data_pending(spwf_id)) {
                int amount;

                amount = _read_in_pkt(spwf_id, false);
                if(amount == SPWFXX_ERR_OOM) { /* consider only 'SPWFXX_ERR_OOM' as non recoverable */
                    return;
                }
            }

            if(!_is_data_pending(spwf_id)) {
                internal_id_cnt++;
                internal_id_cnt %= SPWFSA_SOCKET_COUNT;
            }
        } else {
            internal_id_cnt++;
            internal_id_cnt %= SPWFSA_SOCKET_COUNT;
        }
    }
}

/* Note: returns
 * 'SPWFXX_ERR_OK'   in case of success
 * 'SPWFXX_ERR_OOM'  in case of "out of memory"
 * 'SPWFXX_ERR_READ' in case of `_read_in()` error
 */
int SPWFSAxx::_read_in_packet(int spwf_id, uint32_t amount) {
    struct packet *packet = (struct packet*)malloc(sizeof(struct packet) + amount);
    if (!packet) {
#ifndef NDEBUG
        error("%s(%d): Out of memory!", __func__, __LINE__);
#else // NDEBUG
        debug("%s(%d): Out of memory!", __func__, __LINE__);
#endif
        debug_if(_dbg_on, "\r\nSPWF> %s failed (%d)\r\n", __func__, __LINE__);
        return SPWFXX_ERR_OOM; /* out of memory: give up here! */
    }

    /* init packet */
    packet->id = spwf_id;
    packet->len = amount;
    packet->next = 0;

    /* read data in */
    if(!(_read_in((char*)(packet + 1), spwf_id, amount) > 0)) {
        free(packet);
        debug_if(_dbg_on, "\r\nSPWF> %s failed (%d)\r\n", __func__, __LINE__);
        return SPWFXX_ERR_READ;
    } else {
        debug_if(_dbg_on, "%s():\t%d:%d\r\n", __func__, spwf_id, amount);

        /* append to packet list */
        *_packets_end = packet;
        _packets_end = &packet->next;

        /* remove from pending sizes */
        _remove_pending_pkt_size(spwf_id, amount);

        /* force call of (external) callback */
        _call_callback();
    }

    return SPWFXX_ERR_OK;
}

void SPWFSAxx::_free_packets(int spwf_id) {
    // check if any packets are ready for `spwf_id`
    for(struct packet **p = &_packets; *p;) {
        if ((*p)->id == spwf_id) {
            struct packet *q = *p;
            if (_packets_end == &(*p)->next) {
                _packets_end = p;
            }
            *p = (*p)->next;
            free(q);
        } else {
            p = &(*p)->next;
        }
    }
}

void SPWFSAxx::_free_all_packets() {
    for (int spwf_id = 0; spwf_id < SPWFSA_SOCKET_COUNT; spwf_id++) {
        _free_packets(spwf_id);
    }
}

#define CLOSE_MAX_RETRY (3)
bool SPWFSAxx::close(int spwf_id)
{
    bool ret = false;

    if(((unsigned int)spwf_id) >= ((unsigned int)SPWFSA_SOCKET_COUNT)) {
        goto close_bh_handling; // `ret == false`
    }

    for(int retry_cnt = 0; retry_cnt < CLOSE_MAX_RETRY; retry_cnt++) {
        // Flush out pending data
        while(true) {
            int amount = _read_in_pkt(spwf_id, true);
            if(amount < 0) { // SPWFXX error
                /* empty RX buffer & try to close */
                empty_rx_buffer();
                break;
            }
            if(amount == 0) break; // no more data to be read

            /* immediately free packet (to avoid "out of memory") */
            _free_packets(spwf_id);

            /* interleave bottom halves */
            _execute_bottom_halves();
        }

        // Close socket
        if (_parser.send("AT+S.SOCKC=%d", spwf_id)
                && _recv_ok()) {
            ret = true;
            break; // finish closing
        } else { // close failed
            debug_if(_dbg_on, "\r\nSPWF> %s failed (%d)\r\n", __func__, __LINE__);
            /* interleave bottom halves */
            _execute_bottom_halves();
        }
    }

close_bh_handling:
    /* anticipate bottom halves */
    _execute_bottom_halves();

    if(ret) {
        /* clear pending data flag (should be redundant) */
        _clear_pending_data(spwf_id);

        /* free packets for this socket */
        _free_packets(spwf_id);

        /* reset pending data sizes */
        _reset_pending_pkt_sizes(spwf_id);
    } else {
        debug_if(_dbg_on, "\r\nSPWF> SPWFSAxx::close failed (%d)\r\n", __LINE__);

        int internal_id = _associated_interface.get_internal_id(spwf_id);
        if(!_associated_interface._socket_is_still_connected(internal_id)) {
            /* clear pending data flag (should be redundant) */
            _clear_pending_data(spwf_id);

            /* free packets for this socket */
            _free_packets(spwf_id);

            /* reset pending data sizes */
            _reset_pending_pkt_sizes(spwf_id);

            ret = true;
        }
    }

    return ret;
}

/*
 * Buffered serial event handler
 *
 * Note: executed in IRQ context!
 * Note: call (external) callback only while not receiving
 */
void SPWFSAxx::_event_handler(void)
{
    if(!_is_event_callback_blocked()) {
        _call_callback();
    }
}

/*
 * Common error handler
 */
void SPWFSAxx::_error_handler(void)
{
    if(_parser.recv("%[^\n]\n", _msg_buffer) && _recv_delim_lf()) {
        debug_if(_dbg_on, "AT^ ERROR:%s (%d)\r\n", _msg_buffer, __LINE__);
    } else {
        debug_if(_dbg_on, "\r\nSPWF> Unknown ERROR string in SPWFSAxx::_error_handler (%d)\r\n", __LINE__);
    }

    /* force call of (external) callback */
    _call_callback();
}

/*
 * Handling oob ("+WIND:33:WiFi Network Lost")
 */
void SPWFSAxx::_network_lost_handler_th(void)
{
#ifndef NDEBUG
    static unsigned int net_loss_cnt = 0;
    net_loss_cnt++;
#endif

    _recv_delim_cr_lf();

    debug_if(_dbg_on, "AT^ +WIND:33:WiFi Network Lost\r\n");

#ifndef NDEBUG
    debug_if(_dbg_on, "Getting out of SPWFSAxx::_network_lost_handler_th: %d\r\n", net_loss_cnt);
#else // NDEBUG
    debug_if(_dbg_on, "Getting out of SPWFSAxx::_network_lost_handler_th: %d\r\n", __LINE__);
#endif // NDEBUG

    /* set flag to signal network loss */
    _network_lost_flag = true;

    /* force call of (external) callback */
    _call_callback();

    return;
}

/*
 * Handling oob ("+WIND:55:Pending Data")
 */
void SPWFSAxx::_packet_handler_th(void)
{
    int internal_id, spwf_id;
    int amount;

    /* parse out the socket id & amount */
    if (!(_parser.recv(SPWFXX_RECV_PENDING_DATA, &spwf_id, &amount) && _recv_delim_lf())) {
#ifndef NDEBUG
        error("\r\nSPWFSAxx::%s failed!\r\n", __func__);
#endif
        return;
    }

    debug_if(_dbg_on, "AT^ +WIND:55:Pending Data:%d:%d\r\n", spwf_id, amount);

    /* check for the module to report a valid id */
    MBED_ASSERT(((unsigned int)spwf_id) < ((unsigned int)SPWFSA_SOCKET_COUNT));

    /* set that there is pending data for socket */
    /* NOTE: it seems as if asynchronous indications might report not up-to-date data length values
     *       therefore we just record the socket id without considering the `amount` of data reported!
     */
    internal_id = _associated_interface.get_internal_id(spwf_id);
    if(internal_id != SPWFSA_SOCKET_COUNT) {
        _add_pending_pkt_size(spwf_id, amount);
        _set_pending_data(spwf_id);

        /* force call of (external) callback */
        _call_callback();
    } else {
        debug_if(_dbg_on, "\r\nSPWFSAxx::%s got invalid id %d\r\n", __func__, spwf_id);
    }
}

void SPWFSAxx::_network_lost_handler_bh(void)
{
    if(!_network_lost_flag) return;
    _network_lost_flag = false;

    {
        bool were_connected;
        BlockExecuter netsock_wa_obj(Callback<void()>(this, &SPWFSAxx::_unblock_and_callback),
                                     Callback<void()>(this, &SPWFSAxx::_block_event_callback)); /* call (external) callback only while not receiving */
        Timer timer;
        timer.start();

        _parser.set_timeout(SPWF_NETLOST_TIMEOUT);

        were_connected = isConnected();
        _associated_interface._connected_to_network = false;

        if(were_connected) {
            unsigned int n1, n2, n3, n4;

            while(true) {
                if (timer.read_ms() > SPWF_CONNECT_TIMEOUT) {
                    debug_if(_dbg_on, "\r\nSPWFSAxx::_network_lost_handler_bh() #%d\r\n", __LINE__);
                    disconnect();
                    goto nlh_get_out;
                }

                if((_parser.recv(SPWFXX_RECV_WIFI_UP, &n1, &n2, &n3, &n4)) && _recv_delim_lf()) {
                    debug_if(_dbg_on, "Re-connected (%u.%u.%u.%u)!\r\n", n1, n2, n3, n4);

                    _associated_interface._connected_to_network = true;
                    goto nlh_get_out;
                }
            }
        } else {
            debug_if(_dbg_on, "Leaving SPWFSAxx::_network_lost_handler_bh\r\n");
            goto nlh_get_out;
        }

    nlh_get_out:
        debug_if(_dbg_on, "Getting out of SPWFSAxx::_network_lost_handler_bh\r\n");
        _parser.set_timeout(_timeout);

        return;
    }
}

void SPWFSAxx::_recover_from_hard_faults(void) {
    disconnect();
    _associated_interface.inner_constructor();
    _free_all_packets();
    empty_rx_buffer();

    /* force call of (external) callback */
    _call_callback();
}

/*
 * Handling oob ("+WIND:8:Hard Fault")
 */
void SPWFSAxx::_hard_fault_handler(void)
{
    _parser.set_timeout(SPWF_RECV_TIMEOUT);
    if(_parser.recv("%[^\n]\n", _msg_buffer) && _recv_delim_lf()) {}

#ifndef NDEBUG
    error("\r\nSPWFSAXX hard fault error:\r\n%s\r\n", _msg_buffer);
#else // NDEBUG
    debug("\r\nSPWFSAXX hard fault error:\r\n%s\r\n", _msg_buffer);

    // This is most likely the best we can do to recover from this module hard fault
    _parser.set_timeout(SPWF_HF_TIMEOUT);
    _recover_from_hard_faults();
    _parser.set_timeout(_timeout);
#endif // NDEBUG

    /* force call of (external) callback */
    _call_callback();
}

/*
 * Handling oob ("+WIND:5:WiFi Hardware Failure")
 */
void SPWFSAxx::_wifi_hwfault_handler(void)
{
    unsigned int failure_nr;

    /* parse out the socket id & amount */
    _parser.recv(":%u\n", &failure_nr);
    _recv_delim_lf();

#ifndef NDEBUG
    error("\r\nSPWFSAXX WiFi HW fault error: %u\r\n", failure_nr);
#else // NDEBUG
    debug("\r\nSPWFSAXX WiFi HW fault error: %u\r\n", failure_nr);

    // This is most likely the best we can do to recover from this module hard fault
    _parser.set_timeout(SPWF_HF_TIMEOUT);
    _recover_from_hard_faults();
    _parser.set_timeout(_timeout);
#endif // NDEBUG

    /* force call of (external) callback */
    _call_callback();
}

/*
 * Handling oob ("+WIND:58:Socket Closed")
 * when server closes a client connection
 *
 * NOTE: When a socket client receives an indication about socket server gone (only for TCP sockets, WIND:58),
 *       the socket connection is NOT automatically closed!
 */
void SPWFSAxx::_server_gone_handler(void)
{
    int spwf_id, internal_id;

    if(!(_parser.recv(SPWFXX_RECV_SOCKET_CLOSED, &spwf_id) && _recv_delim_lf())) {
#ifndef NDEBUG
        error("\r\nSPWFSAxx::%s failed!\r\n", __func__);
#endif
        goto _get_out;
    }

    debug_if(_dbg_on, "AT^ +WIND:58:Socket Closed:%d\r\n", spwf_id);

    /* check for the module to report a valid id */
    MBED_ASSERT(((unsigned int)spwf_id) < ((unsigned int)SPWFSA_SOCKET_COUNT));

    /* only set `server_gone`
     * user still can receive data & must still explicitly close the socket
     */
    internal_id = _associated_interface.get_internal_id(spwf_id);
    if(internal_id != SPWFSA_SOCKET_COUNT) {
        _associated_interface._ids[internal_id].server_gone = true;
    }

_get_out:
    /* force call of (external) callback */
    _call_callback();
}

void SPWFSAxx::setTimeout(uint32_t timeout_ms)
{
    _timeout = timeout_ms;
    _parser.set_timeout(timeout_ms);
}

void SPWFSAxx::attach(Callback<void()> func)
{
    _callback_func = func; /* call (external) callback only while not receiving */
}

/**
 *  Recv Function
 */
int32_t SPWFSAxx::recv(int spwf_id, void *data, uint32_t amount, bool datagram)
{
    BH_HANDLER;

    while (true) {
        /* check if any packets are ready for us */
        for (struct packet **p = &_packets; *p; p = &(*p)->next) {
            if ((*p)->id == spwf_id) {
                debug_if(_dbg_on, "\r\nRead done on ID %d and length of packet is %d\r\n",spwf_id,(*p)->len);
                struct packet *q = *p;

                MBED_ASSERT(q->len > 0);

                if(datagram) { // UDP => always remove pkt size
                    // will always consume a whole pending size
                    uint32_t ret;

                    debug_if(_dbg_on, "%s():\t\t\t%d:%d (datagram)\r\n", __func__, spwf_id, q->len);

                    ret = (amount < q->len) ? amount : q->len;
                    memcpy(data, q+1, ret);

                    if (_packets_end == &(*p)->next) {
                        _packets_end = p;
                    }
                    *p = (*p)->next;
                    free(q);

                    return ret;
                } else { // TCP
                    if (q->len <= amount) { // return and remove full packet
                        memcpy(data, q+1, q->len);

                        if (_packets_end == &(*p)->next) {
                            _packets_end = p;
                        }
                        *p = (*p)->next;
                        uint32_t len = q->len;
                        free(q);

                        return len;
                    } else { // `q->len > amount`, return only partial packet
                        if(amount > 0) {
                            memcpy(data, q+1, amount);
                            q->len -= amount;
                            memmove(q+1, (uint8_t*)(q+1) + amount, q->len);
                        }

                        return amount;
                    }
                }
            }
        }

        /* check for pending data on module */
        {
            int len;

            len = _read_in_pkt(spwf_id, false);
            if(len <= 0)  { /* SPWFXX error or no more data to be read */
                return -1;
            }
        }
    }
}

/* Note: returns
 * '>=0'             in case of success, amount of read in data (in bytes)
 * 'SPWFXX_ERR_OOM'  in case of "out of memory"
 * 'SPWFXX_ERR_READ' in case of other `_read_in_packet()` error
 * 'SPWFXX_ERR_LEN'  in case of `_read_len()` error
 */
int SPWFSAxx::_read_in_pkt(int spwf_id, bool close) {
    int pending;
    uint32_t wind_pending;

    BlockExecuter netsock_wa_obj(Callback<void()>(this, &SPWFSAxx::_unblock_event_callback),
                                 Callback<void()>(this, &SPWFSAxx::_block_event_callback)); /* call (external) callback only while not receiving */

    pending = _read_len(spwf_id); // triggers also async indication handling!
    if(close) { // read in all data
        wind_pending = pending;

        /* reset pending data sizes */
        _reset_pending_pkt_sizes(spwf_id);
        /* set new entry for entry size */
        _add_pending_pkt_size(spwf_id, (uint32_t)pending);
    } else { // only read in already notified data
        wind_pending = _get_pending_pkt_size(spwf_id);
    }

    if((pending > 0) && (wind_pending > 0)) {
        MBED_ASSERT(pending >= (int)wind_pending);

        if(pending == (int)wind_pending) {
            _clear_pending_data(spwf_id);
        }

        int ret = _read_in_packet(spwf_id, wind_pending);
        MBED_ASSERT(ret != 0);
        if(ret < 0) { /* "out of memory" or `_read_in_packet()` error */
            /* we do not know if data is still pending at this point
               but leaving the pending data bit set might lead to an endless loop */
            _clear_pending_data(spwf_id);

            return ret;
        }
    } else if(pending < 0) { /* 'SPWFXX_ERR_LEN' error */
        MBED_ASSERT(pending == SPWFXX_ERR_LEN);
        /* we do not know if data is still pending at this point
           but leaving the pending data bit set might lead to an endless loop */
        _clear_pending_data(spwf_id);

        return pending;
    } else if(pending == 0) {
        MBED_ASSERT(wind_pending == 0);
        _clear_pending_data(spwf_id);
    } else if(wind_pending == 0) { // `pending > 0`
        MBED_ASSERT(pending > 0);
        debug_if(_dbg_on, "%s():\t\t%d:%d (WIND missing)\r\n", __func__, spwf_id, pending);

        /* avoid potential endless loop & wait for WIND */
        _clear_pending_data(spwf_id);
    }

    return (int)wind_pending;
}
