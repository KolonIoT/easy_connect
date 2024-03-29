/**
* copyright (c) 2017-2018, James Flynn
* SPDX-License-Identifier: Apache-2.0
*/

/*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
*  @file WNC14A2AInterface.h
*  @brief Implements a standard NetworkInterface class for use with WNC M14A2A 
*  data module. 
*
*  @author James Flynn
* 
*  @date 1-Feb-2018
*  
*/
 
#ifndef WNC14A2A_INTERFACE_H
#define WNC14A2A_INTERFACE_H

#include <stdint.h>

#include "mbed.h"
#include "Callback.h"
#include "WNCDebug.h"
#include "WncControllerK64F/WncControllerK64F.h"

#define WNC14A2A_SOCKET_COUNT WncController::MAX_NUM_WNC_SOCKETS 

// If target board does not support Arduino pins, define pins as Not Connected
#if defined(TARGET_FF_ARDUINO)
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_RX_BOOT_SEL)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RX_BOOT_SEL                     D1
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_POWER_ON)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_POWER_ON                        D2
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_WAKEUP)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_WAKEUP                          D6
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_RESET)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RESET                           D8
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_LVLTRANSLATOR)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_LVLTRANSLATOR                   D9
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_CTS)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_CTS                             D10
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_RXD)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RXD                             D11
#endif
#if !defined(MBED_CONF_WNC14A2A_LIBRARY_WNC_TXD)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_TXD                             D12
#endif
#else // !defined(TARGET_FF_ARDUINO)
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RX_BOOT_SEL                     NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_POWER_ON                        NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_WAKEUP                          NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RESET                           NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_LVLTRANSLATOR                   NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_CTS                             NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_RXD                             NC
#define MBED_CONF_WNC14A2A_LIBRARY_WNC_TXD                             NC
#endif // !defined(TARGET_FF_ARDUINO)

typedef struct smsmsg_t {
        string number;
        string date;
        string time;
        string msg;
    } IOTSMS;

typedef struct socket_t {
        int socket;                         //index of this socket
        string url;
        SocketAddress addr;                 //address info for this socket
        bool opened;                        //is the socket opened
        bool connected;                     //is the socket connected
        int proto;                          //this is a TCP or UDP socket
        void (*_callback)(void*);           //callback used with attach
        void *_cb_data;                     //callback data to be returned
    } WNCSOCKET;

typedef struct rx_event_t {
        int      m_rx_wnc_state;            //state of the socket receive 
        int      m_rx_socket;               //which socket is being rcvd on
        uint8_t *m_rx_dptr;                 //pointer to the users data buffer
        uint32_t m_rx_req_size;             //Requested number of bytes to receive
        uint32_t m_rx_total_cnt;            //Total number of bytes received
        int      m_rx_timer;                //Rx Timeout Timer
        int      m_rx_disTO;                //Flag to disable Timeout Timer
        void    (*m_rx_callback)(void*);    //callback used with attach
        void     *m_rx_cb_data;             //callback data to be returned
        uint32_t m_rx_return_cnt;           //number of bytes the Event Queue is returning
    } RXEVENT;

typedef struct tx_event_t {
    // Transmit Interrupt simulation to enabled non-blocking operation
    int      m_tx_wnc_state;
    int      m_tx_socket;
    uint8_t *m_tx_dptr;
    unsigned m_tx_orig_size;
    uint32_t m_tx_req_size;
    uint32_t m_tx_total_sent;
    void    (*m_tx_callback)(void*);
    void     *m_tx_cb_data;
    } TXEVENT;

#define WNC_DEBUG   0           //1=enable the WNC startup debug output
                                //0=disable the WNC startup debug output
#define STOP_ON_FE  1           //1=hang forever if a fatal error occurs
                                //0=simply return failed response for all socket calls
#define DISPLAY_FE  1           //1 to display the fatal error when it occurs
                                //0 to NOT display the fatal error
#define RESETON_FE  0           //1 to cause the MCU to reset on fatal error
                                //0 to NOT reset the MCU

#define APN_DEFAULT             "m2m.com.attz"
                        
/** Error Handling macros & data
*   @brief  The macros CHK_WNCFE is used to check if a fatal error has occured. If it has
*           then execute the action specified: fail, void, null, resume
*
*    CHK_WNCFE( condition-to-check, fail|void|null|resume )
*
*     'fail'   if you want FATAL_WNC_ERROR to be called.  
*     'void'   if you want to execute a void return
*     'null'   if you want to execute a null return
*     'resume' if you simply want to resume program execution
*
*  There are several settings that control how FATAL_WNC_ERROR behaves:
*      1) RESETON_FE determines if the system will reset or hang.
*      2) DISPLAY_FE determine if an error message is generated or not
*
*  The DISPLAY_FE setting determines if a failure message is displayed. 
*  If set to 1, user sees this messageo:
*
*      WNC FAILED @ source-file-name:source-file-line-number
*
*  if not set, nothing is displayed.
*/

#define FATAL_FLAG  WncController::WNC_NO_RESPONSE
#define WNC_GOOD    WncController::WNC_ON

#define RETfail return -1
#define RETvoid return
#define RETnull return NULL
#define RETresume   

#define DORET(x) RET##x

#define TOSTR(x) #x
#define INTSTR(x) TOSTR(x)
#define FATAL_STR (char*)(__FILE__ ":" INTSTR(__LINE__))

#if RESETON_FE == 1   //reset on fatal error
#define MCURESET     ((*((volatile unsigned long *)0xE000ED0CU))=(unsigned long)((0x5fa<<16) | 0x04L))
#define RSTMSG       "RESET MCU! "
#else
#define MCURESET
#define RSTMSG       ""
#endif

#if DISPLAY_FE == 1  //display fatal error message
#define PFE     {if(_debugUart)_debugUart->printf((char*)RSTMSG "\r\n>>WNC FAILED @ %s\r\n", FATAL_STR);}
#else
#define PFE
#endif

#if STOP_ON_FE == 1  //halt cpu on fatal error
#define FATAL_WNC_ERROR(v)  {_fatal_err_loc=FATAL_STR;PFE;MCURESET;while(1);}
#else
#define FATAL_WNC_ERROR(v)  {_fatal_err_loc=FATAL_STR;PFE;DORET(v);}
#endif

#define CHK_WNCFE(x,y)    if( x ){FATAL_WNC_ERROR(y);}

#define FIRMWARE_REV(x) (((WNC14A2AInterface*)x)->getWNCRev())
#define DBGMSG_DRV	0x04
#define DBGMSG_EQ	0x08
#define DBGMSG_SMS	0x10
#define DBGMSG_ARRY     0x20

#define MAX_SMS_MSGS    3

using namespace WncController_fk;
 
/** WNC14A2AInterface class
 *  Implementation of the NetworkInterface for WNC14A2A 
 */
class WNC14A2AInterface : public NetworkStack, public NetworkInterface
{
public:

    /** WNC14A2AInterface Constructor.
     * @param optionally include a pointer to WNCDEBUG object for 
     * debug information to be displayed.
     */
    WNC14A2AInterface(WNCDebug *_dbgUart = NULL);
    virtual ~WNC14A2AInterface();

    /** Set the cellular network credentials
     *
     *  @param apn      Optional, APN of network
     *  @param user     Optional, username --not used--
     *  @param pass     Optional, password --not used--
     *  @return         nsapi_error_t
     */
    virtual nsapi_error_t set_credentials(const char *apn = 0,
            const char *username = 0, const char *password = 0);
 
    /** Connect to the network
     *
     *  @param apn      Optional, APN of network
     *  @param user     Optional, username --not used--
     *  @param pass     Optional, password --not used--
     *  @return         nsapi_error_t
     */
    virtual nsapi_error_t connect(const char *apn,
            const char *username = 0, const char *password = 0);
 
    /** Connect to the network (no parameters)
     *
     *  @return         nsapi_error_t
     */
    virtual nsapi_error_t connect();

    /** disconnect from the network
     *
     *  provided for completness, but function does nothing becase
     *  WNC part can not disconnect from network once connected.
     *
     *  @return         nsapi_error_t
     */
    virtual nsapi_error_t disconnect();
 
    /** Get the IP address of WNC device. From NetworkStack Class
     *
     *  @return         IP address string or null 
     */
    virtual const char *get_ip_address();
 
    /** Get the network assigned IP address.
     *
     *  @return         IP address or null 
     */
    const char *get_my_ip_address();

    /** Get the MAC address of the WNC device.  
     *
     *  @return         MAC address of the interface
     */
    virtual const char *get_mac_address();
 
    /** Attach a callback function for when a SMS is recevied
     *
     *  @param          function pointer to call
     */
    void sms_attach(void (*callback)(IOTSMS *));

    /** Set the level of Debug output
     *
     *  @param          bit field 
     *         basic AT command info= 0x01
     *         more AT command info = 0x02
     *         mbed driver info     = 0x04
     *         dump buffers         = 0x08
     *         all debug            = 0x0f
     */
    void doDebug(int v);

    /** Return the WNC reported Firmware Revision
     *
     *  @param          none.
     */
    const char* getWNCRev(void);

    /** Query registered state of WNC
     *
     *  @return         true if registerd, false if not 
     */
    bool registered();

    /** Start the SMS monitoring service
     */
    void sms_start(void);

    /** start listening for incomming SMS messages
     *
     *  @param time in msec to check
     */
    void sms_listen(uint16_t=1000);         // Configure device to listen for text messages 
        
    /** retrieve a SMS message
     *
     *  @param          pointer to an array of IOTSMS messages
     */
    int getSMS(IOTSMS **msg);

    /** send a SMS message
     *
     *  @param          a string containing number to send message to
     *  @param          a string containing message to send
     *  @return         true on success, 0 on failure
     */
    int sendIOTSms(const string&, const string&);

    /** return this devices SMS number
     *
     *  @brief The IOTSMS number used, isn't phone number, it is device ICCID.  
     *
     *  @return          this devices IOTSMS number
     */
    char* getSMSnbr();


protected:

    /** Get Host IP by name. 
     *
     *  @return         nsapi_error_t
     */
    virtual nsapi_error_t gethostbyname(const char* name, SocketAddress *address, nsapi_version_t version);


    /** return a pointer to the NetworkStack object
     *
     *  @return          The underlying NetworkStack object
     */
    virtual NetworkStack *get_stack();

    /** Open a socket. 
     *
     *  @param handle       Handle in which to store new socket
     *  @param proto        Type of socket to open, NSAPI_TCP or NSAPI_UDP
     *  @return             0 on success, negative on failure
     */
    virtual int socket_open(void **handle, nsapi_protocol_t proto);
 
    /** Close the socket. 
     *
     *  @param handle       Socket handle
     *  @return             0 on success, negative on failure
     */
    virtual int socket_close(void *handle);
 
    /** Bind a server socket to a specific port.
     *
     *  @brief              Bind the socket to a specific port
     *  @param handle       Socket handle
     *  @param address      address to listen for 
     *  @return             0;
     */
    virtual int socket_bind(void *handle, const SocketAddress &address);
 
    /** Start listening for incoming connections.
     *
     *  @brief              NOT SUPPORTED
     *  @param handle       Socket handle
     *  @param backlog      Number of pending connections that can be queued up at any
     *                      one time [Default: 1]
     *  @return             NSAPI_ERROR_UNSUPPORTED;
     *  @note This function causes the receive time-out to be ignored resulting in continuous
     *        monitoring of received data. This is non-standard behaviour but it is how the
     *        mbed-cloud servcies use it...
     */
    virtual int socket_listen(void *handle, int backlog);
 
    /** Accept a new connection.
     *
     *  @brief              NOT SUPPORTED
     *  @return             NSAPI_ERROR_UNSUPPORTED;
     */
    virtual int socket_accept(nsapi_socket_t server,
            nsapi_socket_t *handle, SocketAddress *address=0);
 
    /** Connects this socket to the server.
     *
     *  @param handle       Socket handle
     *  @param address      SocketAddress 
     *  @return             0 on success, negative on failure
     */
    virtual int socket_connect(void *handle, const SocketAddress &address);
 
    /** Send data to the remote host.
     *
     *  @param handle       Socket handle
     *  @param data         buffer to send
     *  @param size         length of buffer
     *  @return             Number of bytes written or negative on failure
     *
     *  @note This call is blocking. 
     */
    virtual int socket_send(void *handle, const void *data, unsigned size);
 
    /** Receive data from the remote host.
     *
     *  @param handle       Socket handle
     *  @param data         buffer to store the recived data
     *  @param size         bytes to receive
     *  @return             received bytes received, negative on failure
     *
     *  @note This call is not-blocking 
     */
    virtual int socket_recv(void *handle, void *data, unsigned size);
 
    /** Send a packet to a remote endpoint.
     *
     *  @param handle       Socket handle
     *  @param address      SocketAddress
     *  @param data         data to send
     *  @param size         number of bytes to send
     *  @return the         number of bytes sent or negative on failure
     *
     *  @note This call is blocking.
     */
    virtual int socket_sendto(void *handle, const SocketAddress &address, const void *data, unsigned size);
 
    /** Receive packet remote endpoint
     *
     *  @param handle       Socket handle
     *  @param address      SocketAddress 
     *  @param buffer       buffer to store data to
     *  @param size         number of bytes to receive
     *  @return the         number bytes received or negative on failure
     *
     *  @note This call is not-blocking.
     */
    virtual int socket_recvfrom(void *handle, SocketAddress *address, void *buffer, unsigned size);
 
    /** Register a callback on state change of the socket
     *
     *  @param handle       Socket handle
     *  @param callback     Function to call on state change
     *  @param data         Argument to pass to callback
     *
     *  @note Callback may be called in an interrupt context.
     */
    virtual void socket_attach(void *handle, void (*callback)(void *), void *data);
    
    /** get the status of internal errors
     *
     *  @brief Called after any WNC14A2A operation to determine error specifics 
     *  @param none.
     */
    uint16_t wnc14a2a_chk_error(void) { return m_errors; }

private:

    //! WncController Class for interacting with the 14A2a hardware
    friend class WncControllerK64F;  

    bool     m_wncpoweredup;                //track if WNC has been power-up
    unsigned m_debug;

    WncIpStats myNetStats;                  //maintaint the network statistics
    WncControllerK64F_fk::WncControllerK64F *m_pwnc; //pointer to the WncController instance

    WNCDebug *_debugUart;                     // Serial object for parser to communicate with radio
    char *_fatal_err_loc;                   // holds string containing location of fatal error
    nsapi_error_t m_errors;

    bool m_smsmoning;                       // Track if the SMS monitoring thread is running
    EventQueue sms_queue;                   // Queue used to schedule for SMS checks
    void (*_sms_cb)(IOTSMS *);              // Callback when text message is received. User must define this as 
                                            // a static function because I'm not handling an object offset
    IOTSMS m_MsgText, m_MsgText_array[MAX_SMS_MSGS];       // Used to pass SMS message to the user
    struct WncController::WncSmsList m_smsmsgs;            //use the WncSmsList structure to hold messages

    EventQueue wnc_queue;                   // Queue used to schedule for receiving data
    void handle_sms_event();                // SMS tx/rx handler
    void wnc_eq_event();                    // Simulated ISR
    int  rx_event(RXEVENT *);               // receive data handler
    int  tx_event(TXEVENT *);               // tx data handler

    char _mac_address[NSAPI_MAC_SIZE];      // local Mac
    void _dbOut(const char *format, ...);
    void _dbDump_arry( const uint8_t* data, unsigned int size );

    WNCSOCKET _sockets[WNC14A2A_SOCKET_COUNT];  //WNC supports 8 open sockets but driver only supports 1 currently
    TXEVENT   _socTxS[WNC14A2A_SOCKET_COUNT];
    RXEVENT   _socRxS[WNC14A2A_SOCKET_COUNT];
    Thread    _smsThread, _eqThread;            //Event Queue thread for SMS and Rx/Tx data
    Mutex     _pwnc_mutex;                      
    int       _active_socket;

    UARTSerial mdmUart;
    WncIO wnc_io;
};

#endif

