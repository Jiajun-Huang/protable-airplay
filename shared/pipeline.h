#include "rtp.h"
#include "mdns.h"

/**
 * @brief thred for mdns discovery and response
 *
 */
void mdns_therad(void);

/**
 * @brief thread for RTSP communication response and control
 *
 */
void rtsp_thread(void);

/**
 * @brief thread for RTP packet reception and audio processing
 *
 */
void rtp_thread(void);

/**
 * @brief thread for audio output and playback
 *
 */
void audio_thread(void);