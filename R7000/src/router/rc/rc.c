/*
 * Router rc control script
 *
 * Copyright (C) 2014, Broadcom Corporation. All Rights Reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: rc.c 353863 2012-08-29 03:22:47Z $
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h> /* for open */
#include <string.h>
#include <sys/klog.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/utsname.h> /* for uname */
#include <net/if_arp.h>
#include <dirent.h>

#include <epivers.h>
#include <router_version.h>
#include <mtd.h>
#include <shutils.h>
#include <rc.h>
#include <netconf.h>
#include <nvparse.h>
#include <bcmdevs.h>
#include <bcmparams.h>
#include <bcmnvram.h>
#include <wlutils.h>
#include <ezc.h>
#include <pmon.h>
#if defined(__CONFIG_WAPI__) || defined(__CONFIG_WAPI_IAS__)
#include <wapi_utils.h>
#endif /* __CONFIG_WAPI__ || __CONFIG_WAPI_IAS__ */

/* foxconn added start, zacker, 09/17/2009, @wps_led */
#include <fcntl.h>
#include <wps_led.h>
/* foxconn added end, zacker, 09/17/2009, @wps_led */

/*fxcn added by dennis start,05/03/2012, fixed guest network can't reconnect issue*/
#define MAX_BSSID_NUM       4
#define MIN_BSSID_NUM       2
/*fxcn added by dennis end,05/03/2012, fixed guest network can't reconnect issue*/

#ifdef __CONFIG_NAT__
static void auto_bridge(void);
#endif	/* __CONFIG_NAT__ */

#include <sys/sysinfo.h> /* foxconn wklin added */
#ifdef __CONFIG_EMF__
extern void load_emf(void);
#endif /* __CONFIG_EMF__ */

static void restore_defaults(void);
static void sysinit(void);
static void rc_signal(int sig);
/* Foxconn added start, Wins, 05/16/2011, @RU_IPTV */
#if defined(CONFIG_RUSSIA_IPTV)
static int is_russia_specific_support (void);
static int is_china_specific_support (void); /* Foxconn add, Edward zhang, 09/05/2012, @add IPTV support for PR SKU*/
#endif /* CONFIG_RUSSIA_IPTV */
/* Foxconn added end, Wins, 05/16/2011, @RU_IPTV */
/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
static int getVlanname(char vlanname[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE]);
static int getVlanRule(vlan_rule vlan[C_MAX_VLAN_RULE]);
static int getTokens(char *str, char *delimiter, char token[][C_MAX_TOKEN_SIZE], int maxNumToken);
#endif
/*Foxconn add end, edward zhang, 2013/07/03*/
extern struct nvram_tuple router_defaults[];

#define RESTORE_DEFAULTS() \
	(!nvram_match("restore_defaults", "0") || nvram_invmatch("os_name", "linux"))
#ifdef LINUX_2_6_36
static int
coma_uevent(void)
{
	char *modalias = NULL;
	char lan_ifname[32], *lan_ifnames, *next;

	modalias = getenv("MODALIAS");
	if (!strcmp(modalias, "platform:coma_dev")) {

		/* down WiFi adapter */
		lan_ifnames = nvram_safe_get("lan_ifnames");
		foreach(lan_ifname, lan_ifnames, next) {
			if (!strncmp(lan_ifname, "eth", 3)) {
				eval("wl", "-i", lan_ifname, "down");
			}
		}

		system("echo \"2\" > /proc/bcm947xx/coma");
	}
	return 0;
}
#endif /* LINUX_2_6_36 */

#define RESTORE_DEFAULTS() (!nvram_match("restore_defaults", "0") || nvram_invmatch("os_name", "linux"))

/* for WCN support, Foxconn added start by EricHuang, 12/13/2006 */
static void convert_wlan_params(void)
{
    int config_flag = 0; /* Foxconn add, Tony W.Y. Wang, 01/06/2010 */

    /* Foxconn added start pling 03/05/2010 */
    /* Added for dual band WPS req. for WNDR3400 */
#define MAX_SSID_LEN    32
    char wl0_ssid[64], wl1_ssid[64];

    /* first check how we arrived here?
     * 1. or by "add WPS client" in unconfigured state.
     * 2. by external register configure us, 
     */
    strcpy(wl0_ssid, nvram_safe_get("wl0_ssid"));
    strcpy(wl1_ssid, nvram_safe_get("wl1_ssid"));
    /* Foxconn modified, Tony W.Y. Wang, 03/24/2010 @WPS random ssid setting */
    if (!nvram_match("wps_start", "none") || nvram_match("wps_pbc_conn_success", "1"))
    {
        /* case 1 above, either via pbc, or gui */
        /* In this case, the WPS set both SSID to be
         *  either "NTRG-2.4G_xxx" or "NTRG-5G_xxx".
         * We need to set proper SSID for each radio.
         */
#define RANDOM_SSID_2G  "NTGR-2.4G_"
#define RANDOM_SSID_5G  "NTGR-5G_"

        /* Foxconn modified start pling 05/23/2012 */
        /* Fix a issue where 2.4G radio is disabled, 
         * router uses incorrect random ssid */
        /* if (strncmp(wl0_ssid, RANDOM_SSID_2G, strlen(RANDOM_SSID_2G)) == 0) */
                if (strncmp(wl0_ssid, RANDOM_SSID_2G, strlen(RANDOM_SSID_2G)) == 0 && !nvram_match("wl0_radio","0"))
        {
            printf("Random ssid 2.4G\n");
            /* Set correct ssid for 5G */
            sprintf(wl1_ssid, "%s%s", RANDOM_SSID_5G, 
                    &wl0_ssid[strlen(RANDOM_SSID_2G)]);
            nvram_set("wl1_ssid", wl1_ssid);
        }
        else
        if (strncmp(wl1_ssid, RANDOM_SSID_5G, strlen(RANDOM_SSID_5G)) == 0 && !nvram_match("wl1_radio","0"))
        {
            printf("Random ssid 5G\n");
            /* Set correct ssid for 2.4G */
            sprintf(wl0_ssid, "%s%s", RANDOM_SSID_2G, 
                    &wl1_ssid[strlen(RANDOM_SSID_5G)]);
            nvram_set("wl0_ssid", wl0_ssid);
        
            /* Foxconn added start pling 05/23/2012 */
            /* Fix a issue where 2.4G radio is disabled, 
             * router uses incorrect random ssid/passphrase.
             */
            if (nvram_match("wl0_radio","0"))
                nvram_set("wl0_wpa_psk", nvram_get("wl1_wpa_psk"));
            /* Foxconn added end pling 05/23/2012 */
        }
        nvram_unset("wps_pbc_conn_success");
    }
    else
    {
        /* case 2 */
        /* now check whether external register is from:
         * 1. UPnP,
         * 2. 2.4GHz radio
         * 3. 5GHz radio
         */
        if (nvram_match("wps_is_upnp", "1"))
        {
            /* Case 1: UPnP: wired registrar */
            /* SSID for both interface should be same already.
             * So nothing to do.
             */
            printf("Wired External registrar!\n");
        }
        else
        if (nvram_match("wps_currentRFband", "1"))
        {
            /* Case 2: 2.4GHz radio */
            /* Need to add "-5G" to the SSID of the 5GHz band */
            char ssid_suffix[] = "-5G";
            if (MAX_SSID_LEN - strlen(wl0_ssid) >= strlen(ssid_suffix))
            {
                printf("2.4G Wireless External registrar 1!\n");
                /* SSID is not long, so append suffix to wl1_ssid */
                sprintf(wl1_ssid, "%s%s", wl0_ssid, ssid_suffix);
            }
            else
            {
                printf("2.4G Wireless External registrar 2!\n");
                /* SSID is too long, so replace last few chars of ssid
                 * with suffix
                 */
                strcpy(wl1_ssid, wl0_ssid);
                strcpy(&wl1_ssid[MAX_SSID_LEN - strlen(ssid_suffix)], ssid_suffix);
            }
            if (strlen(wl1_ssid) > MAX_SSID_LEN)
                printf("Error wl1_ssid too long (%d)!\n", strlen(wl1_ssid));

            nvram_set("wl1_ssid", wl1_ssid);
        }
        else
        if (nvram_match("wps_currentRFband", "2"))
        {
            /* Case 3: 5GHz radio */
            /* Need to add "-2.4G" to the SSID of the 2.4GHz band */
            char ssid_suffix[] = "-2.4G";

            if (MAX_SSID_LEN - strlen(wl1_ssid) >= strlen(ssid_suffix))
            {
                printf("5G Wireless External registrar 1!\n");
                /* SSID is not long, so append suffix to wl1_ssid */
                sprintf(wl0_ssid, "%s%s", wl1_ssid, ssid_suffix);
            }
            else
            {
                printf("5G Wireless External registrar 2!\n");
                /* Replace last few chars ssid with suffix */
                /* SSID is too long, so replace last few chars of ssid
                 * with suffix
                 */
                strcpy(wl0_ssid, wl1_ssid);
                strcpy(&wl0_ssid[MAX_SSID_LEN - strlen(ssid_suffix)], ssid_suffix);
            }
            nvram_set("wl0_ssid", wl0_ssid);
        }
        else
            printf("Error! unknown external register!\n");
    }
    /* Foxconn added end pling 03/05/2010 */

    nvram_set("wla_ssid", nvram_safe_get("wl0_ssid"));
    nvram_set("wla_temp_ssid", nvram_safe_get("wl0_ssid"));

    if (( strncmp(nvram_safe_get("wl0_akm"), "psk2", 4) == 0 ) || 
    	  ( strncmp(nvram_safe_get("wl0_akm"), "psk psk2", 7) == 0 ))
    {
        nvram_set("wla_secu_type", "WPA2-PSK");
        nvram_set("wla_temp_secu_type", "WPA2-PSK");
        nvram_set("wla_passphrase", nvram_safe_get("wl0_wpa_psk"));
        nvram_set("wl0_akm", "psk2");


        /* If router changes from 'unconfigured' to 'configured' state by
         * adding a WPS client, the wsc_randomssid and wsc_randomkey will
         * be set. In this case, router should use mixedmode security.
         */
        /* Foxconn added start pling 06/15/2010 */
        if (nvram_match("wl0_crypto", "tkip"))
        {
            /* DTM fix: 
             * Registrar may set to WPA2-PSK TKIP mode.
             * In this case, don't try to modify the
             * security type.
            */
            nvram_unset("wps_mixedmode");
        }
        else
        /* Foxconn added end pling 06/15/2010 */
        if (!nvram_match("wps_randomssid", "") ||
            !nvram_match("wps_randomkey", ""))
        {
        nvram_set("wla_secu_type", "WPA2-PSK");
        nvram_set("wla_temp_secu_type", "WPA2-PSK");
//            nvram_set("wla_secu_type", "WPA-AUTO-PSK");
//            nvram_set("wla_temp_secu_type", "WPA-AUTO-PSK");

            nvram_set("wl0_akm", "psk2");
            nvram_set("wl0_crypto", "aes");

            nvram_set("wps_mixedmode", "2");
            //nvram_set("wps_randomssid", "");
            //nvram_set("wps_randomkey", "");
            config_flag = 1;

        }
        else
        {
            /* Foxconn added start pling 02/25/2007 */
            /* Disable WDS if it is already enabled */
            if (nvram_match("wla_wds_enable", "1"))
            {
                nvram_set("wla_wds_enable",  "0");
                nvram_set("wl0_wds", "");
                nvram_set("wl0_mode", "ap");
            }
            /* Foxconn added end pling 02/25/2007 */
        }
    }
    else if ( strncmp(nvram_safe_get("wl0_akm"), "psk", 3) == 0 )
    {
        nvram_set("wla_secu_type", "WPA-PSK");
        nvram_set("wla_temp_secu_type", "WPA-PSK");
        nvram_set("wla_passphrase", nvram_safe_get("wl0_wpa_psk"));

        /* If router changes from 'unconfigured' to 'configured' state by
         * adding a WPS client, the wsc_randomssid and wsc_randomkey will
         * be set. In this case, router should use mixedmode security.
         */
        /* Foxconn added start pling 06/15/2010 */
        if (nvram_match("wl0_crypto", "aes"))
        {
            /* DTM fix: 
             * Registrar may set to WPA-PSK AES mode.
             * In this case, don't try to modify the
             * security type.
            */
            nvram_unset("wps_mixedmode");
        }
        else
        /* Foxconn added end pling 06/15/2010 */
        if (!nvram_match("wps_randomssid", "") ||
            !nvram_match("wps_randomkey", ""))
        {
            /* Foxconn add start, Tony W.Y. Wang, 11/30/2009 */
            /* WiFi TKIP changes for WNDR3400*/
            /*
            When external registrar configures our router as WPA-PSK [TKIP], security, 
            we auto change the wireless mode to Up to 54Mbps. This should only apply to
            router when router is in "WPS Unconfigured" state.
            */
            nvram_set("wla_mode",  "g and b");

            /* Disable 11n support, copied from bcm_wlan_util.c */
            acosNvramConfig_set("wl_nmode", "0");
            acosNvramConfig_set("wl0_nmode", "0");

            acosNvramConfig_set("wl_gmode", "1");
            acosNvramConfig_set("wl0_gmode", "1");

            /* Set bandwidth to 20MHz */
#if ( !(defined BCM4718) && !(defined BCM4716) && !(defined R6300v2) && !defined(R6250) && !defined(R6200v2) && !defined(R7000))
            acosNvramConfig_set("wl_nbw", "20");
            acosNvramConfig_set("wl0_nbw", "20");
#endif
        
            acosNvramConfig_set("wl_nbw_cap", "0");
            acosNvramConfig_set("wl0_nbw_cap", "0");

            /* Disable extension channel */
            acosNvramConfig_set("wl_nctrlsb", "none");
            acosNvramConfig_set("wl0_nctrlsb", "none");

            /* Now set the security */
            nvram_set("wla_secu_type", "WPA-PSK");
            nvram_set("wla_temp_secu_type", "WPA-PSK");

            nvram_set("wl0_akm", "psk ");
            nvram_set("wl0_crypto", "tkip");

            /*
            nvram_set("wla_secu_type", "WPA-AUTO-PSK");
            nvram_set("wla_temp_secu_type", "WPA-AUTO-PSK");

            nvram_set("wl0_akm", "psk psk2 ");
            nvram_set("wl0_crypto", "tkip+aes");
            */
            /* Foxconn add end, Tony W.Y. Wang, 11/30/2009 */
            nvram_set("wps_mixedmode", "1");
            //nvram_set("wps_randomssid", "");
            //nvram_set("wps_randomkey", "");
            config_flag = 1;
            /* Since we changed to mixed mode, 
             * so we need to disable WDS if it is already enabled
             */
            if (nvram_match("wla_wds_enable", "1"))
            {
                nvram_set("wla_wds_enable",  "0");
                nvram_set("wl0_wds", "");
                nvram_set("wl0_mode", "ap");
            }
        }
    }
    else if ( strncmp(nvram_safe_get("wl0_wep"), "enabled", 7) == 0 )
    {
        int key_len=0;
        if ( strncmp(nvram_safe_get("wl0_auth"), "1", 1) == 0 ) /*shared mode*/
        {
            nvram_set("wla_auth_type", "sharedkey");
            nvram_set("wla_temp_auth_type", "sharedkey");
        }
        else
        {
            nvram_set("wla_auth_type", "opensystem");
            nvram_set("wla_temp_auth_type", "opensystem");
        }
        
        nvram_set("wla_secu_type", "WEP");
        nvram_set("wla_temp_secu_type", "WEP");
        nvram_set("wla_defaKey", "0");
        nvram_set("wla_temp_defaKey", "0");
        /* Foxconn add start by aspen Bai, 02/24/2009 */
        /*
        nvram_set("wla_key1", nvram_safe_get("wl_key1"));
        nvram_set("wla_temp_key1", nvram_safe_get("wl_key1"));
        
        printf("wla_wep_length: %d\n", strlen(nvram_safe_get("wl_key1")));
        
        key_len = atoi(nvram_safe_get("wl_key1"));
        */
        nvram_set("wla_key1", nvram_safe_get("wl0_key1"));
        nvram_set("wla_temp_key1", nvram_safe_get("wl0_key1"));
        
        printf("wla_wep_length: %d\n", strlen(nvram_safe_get("wl0_key1")));
        
        key_len = strlen(nvram_safe_get("wl0_key1"));
        /* Foxconn add end by aspen Bai, 02/24/2009 */
        if (key_len==5 || key_len==10)
        {
            nvram_set("wla_wep_length", "1");
        }
        else
        {
            nvram_set("wla_wep_length", "2");
        }
        /* Foxconn add start by aspen Bai, 02/24/2009 */
        if (key_len==5 || key_len==13)
        {
            char HexKeyArray[32];
            char key[32], tmp[32];
            int i;
            
            strcpy(key, nvram_safe_get("wl0_key1"));
            memset(HexKeyArray, 0, sizeof(HexKeyArray));
            for (i=0; i<key_len; i++)
            {
                sprintf(tmp, "%02X", (unsigned char)key[i]);
                strcat(HexKeyArray, tmp);
            }
            printf("ASCII WEP key (%s) convert -> HEX WEP key (%s)\n", key, HexKeyArray);
            
            nvram_set("wla_key1", HexKeyArray);
            nvram_set("wla_temp_key1", HexKeyArray);
        }
        /* Foxconn add end by aspen Bai, 02/24/2009 */
    }
    else
    {
        nvram_set("wla_secu_type", "None");
        nvram_set("wla_temp_secu_type", "None");
        nvram_set("wla_passphrase", "");
    }
    /* Foxconn add start, Tony W.Y. Wang, 11/23/2009 */
    nvram_set("wlg_ssid", nvram_safe_get("wl1_ssid"));
    nvram_set("wlg_temp_ssid", nvram_safe_get("wl1_ssid"));

    if ( (strncmp(nvram_safe_get("wl1_akm"), "psk2", 4) == 0) || 
    	( strncmp(nvram_safe_get("wl1_akm"), "psk psk2", 7) == 0 ))
    {
        nvram_set("wlg_secu_type", "WPA2-PSK");
        nvram_set("wlg_temp_secu_type", "WPA2-PSK");
        nvram_set("wl1_akm", "psk2");
        nvram_set("wlg_passphrase", nvram_safe_get("wl1_wpa_psk"));


        /* If router changes from 'unconfigured' to 'configured' state by
         * adding a WPS client, the wsc_randomssid and wsc_randomkey will
         * be set. In this case, router should use mixedmode security.
         */

        /* Foxconn added start pling 06/15/2010 */
        if (nvram_match("wl1_crypto", "tkip"))
        {
            /* DTM fix: 
             * Registrar may set to WPA2-PSK TKIP mode.
             * In this case, don't try to modify the
             * security type.
            */
            nvram_unset("wps_mixedmode");
        }
        else
        /* Foxconn added end pling 06/15/2010 */
        if (!nvram_match("wps_randomssid", "") ||
            !nvram_match("wps_randomkey", ""))
        {
            nvram_set("wlg_secu_type", "WPA2-PSK");
            nvram_set("wlg_temp_secu_type", "WPA2-PSK");

            nvram_set("wl1_akm", "psk2");
            nvram_set("wl1_crypto", "aes");

            nvram_unset("wps_mixedmode");
            //nvram_set("wps_randomssid", "");
            //nvram_set("wps_randomkey", "");
            config_flag = 1;

        }
        else
        {
            /* Foxconn added start pling 02/25/2007 */
            /* Disable WDS if it is already enabled */
            if (nvram_match("wlg_wds_enable", "1"))
            {
                nvram_set("wlg_wds_enable",  "0");
                nvram_set("wl1_wds", "");
                nvram_set("wl1_mode", "ap");
            }
            /* Foxconn added end pling 02/25/2007 */
        }
    }
    else if ( strncmp(nvram_safe_get("wl1_akm"), "psk", 3) == 0 )
    {
        nvram_set("wlg_secu_type", "WPA-PSK");
        nvram_set("wlg_temp_secu_type", "WPA-PSK");
        nvram_set("wlg_passphrase", nvram_safe_get("wl1_wpa_psk"));

        /* If router changes from 'unconfigured' to 'configured' state by
         * adding a WPS client, the wsc_randomssid and wsc_randomkey will
         * be set. In this case, router should use mixedmode security.
         */

        /* Foxconn added start pling 06/15/2010 */
        if (nvram_match("wl1_crypto", "aes"))
        {
            /* DTM fix: 
             * Registrar may set to WPA-PSK AES mode.
             * In this case, don't try to modify the
             * security type.
            */
            nvram_unset("wps_mixedmode");
        }
        else
        /* Foxconn added end pling 06/15/2010 */
        if (!nvram_match("wps_randomssid", "") ||
            !nvram_match("wps_randomkey", ""))
        {
            /* Foxconn add start, Tony W.Y. Wang, 11/30/2009 */
            /* WiFi TKIP changes for WNDR3400*/
            /*
            When external registrar configures our router as WPA-PSK [TKIP], security, 
            we auto change the wireless mode to Up to 54Mbps. This should only apply to
            router when router is in "WPS Unconfigured" state.
            */
            nvram_set("wlg_mode",  "g and b");

            /* Disable 11n support, copied from bcm_wlan_util.c */
            acosNvramConfig_set("wl1_nmode", "0");

            acosNvramConfig_set("wl1_gmode", "1");

            /* Set bandwidth to 20MHz */
#if ( !(defined BCM4718) && !(defined BCM4716) && !(defined R6300v2) && !defined(R6250) && !defined(R6200v2) && !defined(R7000))
            acosNvramConfig_set("wl1_nbw", "20");
#endif
        
            acosNvramConfig_set("wl1_nbw_cap", "0");

            /* Disable extension channel */
            acosNvramConfig_set("wl1_nctrlsb", "none");

            /* Now set the security */
            nvram_set("wlg_secu_type", "WPA-PSK");
            nvram_set("wlg_temp_secu_type", "WPA-PSK");

            nvram_set("wl1_akm", "psk ");
            nvram_set("wl1_crypto", "tkip");
            /*
            nvram_set("wlg_secu_type", "WPA-AUTO-PSK");
            nvram_set("wlg_temp_secu_type", "WPA-AUTO-PSK");

            nvram_set("wl1_akm", "psk psk2 ");
            nvram_set("wl1_crypto", "tkip+aes");
            */
            /* Foxconn add end, Tony W.Y. Wang, 11/30/2009 */
            nvram_set("wps_mixedmode", "1");
            //nvram_set("wps_randomssid", "");
            //nvram_set("wps_randomkey", "");
            config_flag = 1;
            /* Since we changed to mixed mode, 
             * so we need to disable WDS if it is already enabled
             */
            if (nvram_match("wlg_wds_enable", "1"))
            {
                nvram_set("wlg_wds_enable",  "0");
                nvram_set("wl1_wds", "");
                nvram_set("wl1_mode", "ap");
            }
        }
    }
    else if ( strncmp(nvram_safe_get("wl1_wep"), "enabled", 7) == 0 )
    {
        int key_len=0;
        if ( strncmp(nvram_safe_get("wl1_auth"), "1", 1) == 0 ) /*shared mode*/
        {
            nvram_set("wlg_auth_type", "sharedkey");
            nvram_set("wlg_temp_auth_type", "sharedkey");
        }
        else
        {
            nvram_set("wlg_auth_type", "opensystem");
            nvram_set("wlg_temp_auth_type", "opensystem");
        }
        
        nvram_set("wlg_secu_type", "WEP");
        nvram_set("wlg_temp_secu_type", "WEP");
        nvram_set("wlg_defaKey", "0");
        nvram_set("wlg_temp_defaKey", "0");
        /* Foxconn add start by aspen Bai, 02/24/2009 */
        /*
        nvram_set("wla_key1", nvram_safe_get("wl_key1"));
        nvram_set("wla_temp_key1", nvram_safe_get("wl_key1"));
        
        printf("wla_wep_length: %d\n", strlen(nvram_safe_get("wl_key1")));
        
        key_len = atoi(nvram_safe_get("wl_key1"));
        */
        nvram_set("wlg_key1", nvram_safe_get("wl1_key1"));
        nvram_set("wlg_temp_key1", nvram_safe_get("wl1_key1"));
        
        printf("wlg_wep_length: %d\n", strlen(nvram_safe_get("wl1_key1")));
        
        key_len = strlen(nvram_safe_get("wl1_key1"));
        /* Foxconn add end by aspen Bai, 02/24/2009 */
        if (key_len==5 || key_len==10)
        {
            nvram_set("wlg_wep_length", "1");
        }
        else
        {
            nvram_set("wlg_wep_length", "2");
        }
        /* Foxconn add start by aspen Bai, 02/24/2009 */
        if (key_len==5 || key_len==13)
        {
            char HexKeyArray[32];
            char key[32], tmp[32];
            int i;
            
            strcpy(key, nvram_safe_get("wl1_key1"));
            memset(HexKeyArray, 0, sizeof(HexKeyArray));
            for (i=0; i<key_len; i++)
            {
                sprintf(tmp, "%02X", (unsigned char)key[i]);
                strcat(HexKeyArray, tmp);
            }
            printf("ASCII WEP key (%s) convert -> HEX WEP key (%s)\n", key, HexKeyArray);
            
            nvram_set("wlg_key1", HexKeyArray);
            nvram_set("wlg_temp_key1", HexKeyArray);
        }
        /* Foxconn add end by aspen Bai, 02/24/2009 */
    }
    else
    {
        nvram_set("wlg_secu_type", "None");
        nvram_set("wlg_temp_secu_type", "None");
        nvram_set("wlg_passphrase", "");
    }
    
    if (config_flag == 1)
    {
        //nvram_set("wps_randomssid", "");
        //nvram_set("wps_randomkey", "");
        nvram_set("wl0_wps_config_state", "1");
        nvram_set("wl1_wps_config_state", "1");
    }
    /* Foxconn add end, Tony W.Y. Wang, 11/23/2009 */
    nvram_set("allow_registrar_config", "0");  /* Foxconn added pling, 05/16/2007 */

    /* Foxconn added start pling 02/25/2008 */
    /* 'wl_unit' is changed to "0.-1" after Vista configure router (using Borg DTM1.3 patch).
     * This will make WPS fail to work on the correct interface.
     * Set it back to "0" if it is not.
     */
    if (!nvram_match("wl_unit", "0"))
        nvram_set("wl_unit", "0");
    /* Foxconn added end pling 02/25/2008 */
}
/* Foxconn added end by EricHuang, 12/13/2006 */

/* foxconn added start wklin, 11/02/2006 */
static void save_wlan_time(void)
{
    struct sysinfo info;
    char command[128];
    sysinfo(&info);
    sprintf(command, "echo %lu > /tmp/wlan_time", info.uptime);
    system(command);
    return;
}
/* foxconn added end, wklin, 11/02/2006 */

/* foxconn added start, zacker, 01/13/2012, @iptv_igmp */
#ifdef CONFIG_RUSSIA_IPTV
static int config_iptv_params(void)
{
#ifdef VLAN_SUPPORT
    unsigned int enabled_vlan_ports = 0x00;
#if defined(R8000)
    unsigned int iptv_bridge_intf = 0x00;
#else
    unsigned char iptv_bridge_intf = 0x00;
#endif
#endif
    char vlan1_ports[16] = "";
    char vlan_iptv_ports[16] = "";
    /*added by dennis start,05/04/2012,for guest network reconnect issue*/
    char br0_ifnames[64]="";
    char if_name[16]="";
    char wl_param[16]="";
    char command[128]="";
    int i = 0;
    /*added by dennis end,05/04/2012,for guest network reconnect issue*/

/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
    char br_ifname[16] = "";
    char br_ifnames[64] = "";
    char clean_vlan[16] = "";
    char clean_vlan_hw[16] = "";
    /*clean up the nvram ,to let the new config work*/

    if (nvram_match ("enable_vlan", "enable"))
    {
        for(i=1; i<7; i++)
        {
            sprintf(br_ifname,"lan%d_ifname",i);
            sprintf(br_ifnames,"lan%d_ifnames",i);
            nvram_set(br_ifnames, "");
            nvram_set(br_ifname, "");
        }
        for(i=1; i < 4094; i++)
        {
            sprintf(clean_vlan,"vlan%dports",i);
            sprintf(clean_vlan_hw,"vlan%dhwname",i);
            if( i == 1 || i == 2)
            {
              nvram_set(clean_vlan,"");
              nvram_set(clean_vlan_hw,"");
            }
            else
            {
                nvram_unset(clean_vlan);
                nvram_unset(clean_vlan_hw);
            }
        }
    }
    else
    {
        for(i=3; i < 4094; i++)
        {
            sprintf(clean_vlan,"vlan%dports",i);
            sprintf(clean_vlan_hw,"vlan%dhwname",i);
            nvram_unset(clean_vlan);
            nvram_unset(clean_vlan_hw);
        }
    }

#endif
/*Foxconn add end, edward zhang, 2013/07/03*/

    if (nvram_match(NVRAM_IPTV_ENABLED, "1"))
    {
        char iptv_intf[32];

        strcpy(iptv_intf, nvram_safe_get(NVRAM_IPTV_INTF));
#if defined(R8000)
        sscanf(iptv_intf, "0x%04X", &iptv_bridge_intf);
#else
        sscanf(iptv_intf, "0x%02X", &iptv_bridge_intf);
#endif        
    }

    /* Foxconn modified start pling 04/03/2012 */
    /* Swap LAN1 ~ LAN4 due to reverse labeling */
#if defined(R7000)
    if (iptv_bridge_intf & IPTV_LAN1)
        strcat(vlan_iptv_ports, "1 ");
    else
        strcat(vlan1_ports, "1 ");

    if (iptv_bridge_intf & IPTV_LAN2)   /* Foxconn modified pling 02/09/2012, fix a typo */
        strcat(vlan_iptv_ports, "2 ");
    else
        strcat(vlan1_ports, "2 ");

    if (iptv_bridge_intf & IPTV_LAN3)
        strcat(vlan_iptv_ports, "3 ");
    else
        strcat(vlan1_ports, "3 ");
    
    if (iptv_bridge_intf & IPTV_LAN4)
        strcat(vlan_iptv_ports, "4 ");
    else
        strcat(vlan1_ports, "4 ");
#else
    if (iptv_bridge_intf & IPTV_LAN1)
        strcat(vlan_iptv_ports, "3 ");
    else
        strcat(vlan1_ports, "3 ");

    if (iptv_bridge_intf & IPTV_LAN2)   /* Foxconn modified pling 02/09/2012, fix a typo */
        strcat(vlan_iptv_ports, "2 ");
    else
        strcat(vlan1_ports, "2 ");

    if (iptv_bridge_intf & IPTV_LAN3)
        strcat(vlan_iptv_ports, "1 ");
    else
        strcat(vlan1_ports, "1 ");
    
    if (iptv_bridge_intf & IPTV_LAN4)
        strcat(vlan_iptv_ports, "0 ");
    else
        strcat(vlan1_ports, "0 ");
    /* Foxconn modified end pling 04/03/2012 */
#endif
    strcat(vlan1_ports, "5*");
/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
    char lan_interface[16]="";
    char lan_hwname[16]="";
    if (nvram_match ("enable_vlan", "enable"))
    {
        sprintf(lan_interface,"vlan%sports",nvram_safe_get("vlan_lan_id"));
        nvram_set(lan_interface,vlan1_ports);
        sprintf(lan_hwname,"vlan%shwname",nvram_safe_get("vlan_lan_id"));
        nvram_set(lan_hwname,"et0");
    }
    else
#endif
/*Foxconn add end, edward zhang, 2013/07/03*/
    nvram_set("vlan1ports", vlan1_ports);

    /* build vlan3 for IGMP snooping on IPTV ports */
/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
    if (nvram_match ("enable_vlan", "enable"))
        ;/*do nothing*/
    else
    {
#endif
/*Foxconn add end, edward zhang, 2013/07/03*/
        if (strlen(vlan_iptv_ports))
        {
            strcat(vlan_iptv_ports, "5");
            nvram_set("vlan3ports", vlan_iptv_ports);
            nvram_set("vlan3hwname", nvram_safe_get("vlan2hwname"));
        }
        else
        {
            nvram_unset("vlan3ports");
            nvram_unset("vlan3hwname");
        }
#ifdef VLAN_SUPPORT
    }
#endif
/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
    
    if (nvram_match ("enable_vlan", "enable"))
    {
        char vlan_ifname[16] = "";
        char vlan_ifname_ports[16] = "";
        char vlan_ports[16]  = "";
        char vlan_prio[16] = "";
        char vlan_hwname[16] = "";
        char wan_vlan_ifname[16] = "";
        char lan_vlan_hwname[16] = "";
        vlan_rule vlan[C_MAX_VLAN_RULE];
        int numVlanRule = getVlanRule(vlan);
       unsigned int vlan_bridge_intf = 0x00;
        char lan_vlan_ports[16] = "";
        char lan_ports[16] = "";
        int lan_vlan_port = 4;
        int lan_vlan_br = 1;
        char lan_vlan_ifname[16] = "";
        char lan_vlan_ifnames[128] = "";
        char lan_ifnames[128] = "";
        char lan_ifname[16] = "";
        int internet_vlan_id;

        /* always set emf_enable to 0 when vlan is enable*/
        nvram_set("emf_enable", "0");

        cprintf("rule_num:%d \n",numVlanRule);
        sprintf(lan_ifnames,"%s ",nvram_safe_get("lan_interface"));
        for(i=0;i<numVlanRule;i++)
        {
            memset(lan_vlan_ifnames,0,sizeof(lan_vlan_ifnames));
            memset(vlan_ports,0,sizeof(vlan_ports));
            if(!strcmp(vlan[i].enable_rule,"0"))
                continue;
            sprintf(vlan_ifname,"vlan%s ",vlan[i].vlan_id);
            sprintf(wan_vlan_ifname,"vlan%s",vlan[i].vlan_id);
            sprintf(vlan_ifname_ports,"vlan%sports",vlan[i].vlan_id);
            sprintf(vlan_hwname,"vlan%shwname",vlan[i].vlan_id);
            nvram_set(vlan_hwname,"et0");
            sprintf(vlan_prio,"vlan%s_prio",vlan[i].vlan_id);
            nvram_set(vlan_prio,vlan[i].vlan_prio);
            
            if(!strcmp(vlan[i].vlan_name, "Internet"))
            {
#if defined(R7000)
         	    nvram_set(vlan_ifname_ports,"0t 5");
#else
         	    nvram_set(vlan_ifname_ports,"4t 5");
#endif
                nvram_set("internet_prio",vlan[i].vlan_prio);
                nvram_set("internet_vlan",vlan[i].vlan_id);
                nvram_set("wan_ifnames", vlan_ifname);
                nvram_set("wan_ifname", wan_vlan_ifname);
                internet_vlan_id=atoi(vlan[i].vlan_id);
                continue;
            }
            
            if(internet_vlan_id==atoi(vlan[i].vlan_id))
            {
                nvram_set("wan_ifnames", "br1");
                nvram_set("wan_ifname", "br1");
            }
            
#if defined(R8000)
            sscanf(vlan[i].vlan_ports, "0x%04X", &vlan_bridge_intf);
#else
            sscanf(vlan[i].vlan_ports, "0x%02X", &vlan_bridge_intf);
#endif            
            strcat(lan_vlan_ifnames, vlan_ifname);
            enabled_vlan_ports |= vlan_bridge_intf ;
#if defined(R7000)
            if (vlan_bridge_intf & IPTV_LAN1)
                strcat(vlan_ports, "1 ");

            if (vlan_bridge_intf & IPTV_LAN2)  
                strcat(vlan_ports, "2 ");

            if (vlan_bridge_intf & IPTV_LAN3)
                strcat(vlan_ports, "3 ");
            
            if (vlan_bridge_intf & IPTV_LAN4)
                strcat(vlan_ports, "4 ");
            
            strcat(vlan_ports, "0t 5");
#else
            if (vlan_bridge_intf & IPTV_LAN1)
                strcat(vlan_ports, "3 ");

            if (vlan_bridge_intf & IPTV_LAN2)  
                strcat(vlan_ports, "2 ");

            if (vlan_bridge_intf & IPTV_LAN3)
                strcat(vlan_ports, "1 ");
            
            if (vlan_bridge_intf & IPTV_LAN4)
                strcat(vlan_ports, "0 ");
            
            strcat(vlan_ports, "4t 5");
#endif
            nvram_set(vlan_ifname_ports,vlan_ports);    /*Foxconn add, edward zhang ,set the bridge ports*/
                

            if (vlan_bridge_intf & IPTV_WLAN1)
                strcat(lan_vlan_ifnames, "eth1 ");

            if (vlan_bridge_intf & IPTV_WLAN2)
                strcat(lan_vlan_ifnames, "eth2 ");


            if (vlan_bridge_intf & IPTV_WLAN_GUEST1)
                strcat(lan_vlan_ifnames, "wl0.1 ");

            if (vlan_bridge_intf & IPTV_WLAN_GUEST2)
                strcat(lan_vlan_ifnames, "wl1.1 ");

            
            sprintf(br_ifname,"lan%d_ifname",lan_vlan_br);
            sprintf(br_ifnames,"lan%d_ifnames",lan_vlan_br);
            sprintf(lan_vlan_ifname,"br%d",lan_vlan_br);
            nvram_set(br_ifname,lan_vlan_ifname);
            nvram_set(br_ifnames,lan_vlan_ifnames);
            lan_vlan_br++;
        }
#if defined(R7000)
        
        if (!(enabled_vlan_ports & IPTV_LAN1))
            strcat(lan_ports, "1 ");

        if (!(enabled_vlan_ports & IPTV_LAN2))  
            strcat(lan_ports, "2 ");

        if (!(enabled_vlan_ports & IPTV_LAN3))
            strcat(lan_ports, "3 ");
            
        if (!(enabled_vlan_ports & IPTV_LAN4))
            strcat(lan_ports, "4 ");
#else
        if (!(enabled_vlan_ports & IPTV_LAN1))
            strcat(lan_ports, "3 ");

        if (!(enabled_vlan_ports & IPTV_LAN2))  
            strcat(lan_ports, "2 ");

        if (!(enabled_vlan_ports & IPTV_LAN3))
            strcat(lan_ports, "1 ");
            
        if (!(enabled_vlan_ports & IPTV_LAN4))
            strcat(lan_ports, "0 ");
#endif
            
        strcat(lan_ports, "5*");
        nvram_set(lan_interface,lan_ports);
        
        if (!(enabled_vlan_ports & IPTV_WLAN1))
            strcat(lan_ifnames, "eth1 ");

        if (!(enabled_vlan_ports & IPTV_WLAN2))
            strcat(lan_ifnames, "eth2 ");

        
        strcpy(br0_ifnames,lan_ifnames);
#ifdef __CONFIG_IGMP_SNOOPING__
        /* always enable snooping for VLAN IPTV */
        //nvram_set("emf_enable", "1");
#endif
#ifdef VLAN_SUPPORT
            nvram_set("vlan2hwname", "et0");
            nvram_set("vlan1hwname", "et0");
#endif
    }
	else
#endif
/*Foxconn add end, edward zhang, 2013/07/03*/
    if (iptv_bridge_intf & IPTV_MASK)
    {
        char lan_ifnames[128] = "vlan1 ";
        char wan_ifnames[128] = "vlan2 ";
    
#ifdef __CONFIG_IGMP_SNOOPING__
        /* always enable snooping for IPTV */
        nvram_set("emf_enable", "1");
#endif

        /* always build vlan2 and br1 and enable vlan tag output for all vlan */
#if ( defined(R7000))
        nvram_set("vlan2ports", "0 5");
#else
        nvram_set("vlan2ports", "4 5");
#endif
        /* build vlan3 for IGMP snooping on IPTV ports */
        if (strlen(vlan_iptv_ports))
            strcat(wan_ifnames, "vlan3 ");

        if (iptv_bridge_intf & IPTV_WLAN1)
            strcat(wan_ifnames, "eth1 ");
        else
            strcat(lan_ifnames, "eth1 ");

        if (iptv_bridge_intf & IPTV_WLAN2)
            strcat(wan_ifnames, "eth2 ");
        else
            strcat(lan_ifnames, "eth2 ");


        if (iptv_bridge_intf & IPTV_WLAN_GUEST1)
            strcat(wan_ifnames, "wl0.1 ");
        else
            strcat(lan_ifnames, "wl0.1 ");

        if (iptv_bridge_intf & IPTV_WLAN_GUEST2)
            strcat(wan_ifnames, "wl1.1 ");
        else
            strcat(lan_ifnames, "wl1.1 ");

        //nvram_set("lan_ifnames", lan_ifnames);
        strcpy(br0_ifnames,lan_ifnames);
        nvram_set("wan_ifnames", wan_ifnames);
        nvram_set("lan1_ifnames", wan_ifnames);

        nvram_set("wan_ifname", "br1");
        nvram_set("lan1_ifname", "br1");
    }
    else
    {
        
        //nvram_set("lan_ifnames", "vlan1 eth1 eth2 wl0.1");
        /*modified by dennis start, 05/03/2012,fixed guest network cannot reconnect issue*/
        strcpy(br0_ifnames,"vlan1 eth1 eth2");       
        /*modified by dennis end, 05/03/2012,fixed guest network cannot reconnect issue*/
/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
        nvram_set("vlan2hwname", "et0");
        nvram_set("vlan1hwname", "et0");
#endif
        nvram_set("lan1_ifnames", "");
        nvram_set("lan1_ifname", "");
/*Foxconn add end, edward zhang, 2013/07/03*/
#ifdef __CONFIG_IGMP_SNOOPING__
        /* foxconn Bob modified start 08/26/2013, not to bridge eth0 and vlan1 in the same bridge */
        if (nvram_match("emf_enable", "1") || nvram_match("enable_ap_mode", "1") ) {
        /* foxconn Bob modified end 08/26/2013, not to bridge eth0 and vlan1 in the same bridge */
#if ( defined(R7000))
            nvram_set("vlan2ports", "0 5");
#else
            nvram_set("vlan2ports", "4 5");
#endif
            nvram_set("wan_ifnames", "vlan2 ");
            nvram_set("wan_ifname", "vlan2");
        }
        else
#endif
        {
#if ( defined(R7000))
/* foxconn revise start ken chen @ 08/23/2013, to fix IGMP report duplicated in AP mode*/
            if (nvram_match("enable_ap_mode", "1")) {
                nvram_set("vlan2ports", "0 5");
                nvram_set("wan_ifnames", "vlan2 ");
                nvram_set("wan_ifname", "vlan2");
            }
            /* Foxconn Perry added start, 11/17/2014, for extender mode */
            /* set br0 as wan interface in extender mode */
#ifdef CONFIG_EXTENDER_MODE
            else if(nvram_match("enable_extender_mode", "1")) {   
                nvram_set("vlan2ports", "0 5");
                nvram_set("wan_ifnames", "br0 ");
                nvram_set("wan_ifname", "br0");           
            }
#endif /* CONFIG_EXTENDER_MODE */
            /* Foxconn Perry added end, 11/17/2014, for extender mode */
            else {
                nvram_set("vlan2ports", "0 5u");
                nvram_set("wan_ifnames", "eth0 ");
                nvram_set("wan_ifname", "eth0");
            }
#else
            nvram_set("vlan2ports", "4 5");
//#endif
            
            /* Foxconn Perry added start, 11/17/2014, for extender mode */
            /* set br0 as wan interface in extender mode */
#ifdef CONFIG_EXTENDER_MODE
            if(nvram_match("enable_extender_mode", "1")) { 
                nvram_set("wan_ifnames", "br0 ");
                nvram_set("wan_ifname", "br0");           
            } else {
                nvram_set("wan_ifnames", "eth0 ");
                nvram_set("wan_ifname", "eth0");
            }
#else /* !CONFIG_EXTENDER_MODE */
            nvram_set("wan_ifnames", "eth0 ");
            nvram_set("wan_ifname", "eth0");
#endif /* !CONFIG_EXTENDER_MODE */
            /* Foxconn Perry added end, 11/17/2014, for extender mode */
#endif
/* foxconn revise end ken chen @ 08/23/2013, to fix IGMP report duplicated in AP mode*/
        }
    }

     /*added by dennis start, 05/03/2012,fixed guest network cannot reconnect issue*/
     for(i = MIN_BSSID_NUM; i <= MAX_BSSID_NUM; i++){
        sprintf(wl_param, "%s_%d", "wla_sec_profile_enable", i);     
        if(nvram_match(wl_param, "1")){
            sprintf(if_name, "wl0.%d", i-1);
            if(nvram_match("enable_vlan", "enable"))
            {
                if(!(enabled_vlan_ports & IPTV_WLAN_GUEST1))
                {
                    strcat(br0_ifnames, " ");
                    strcat(br0_ifnames, if_name);
                }
            }
            else if (nvram_match(NVRAM_IPTV_ENABLED, "1"))
            {
            	// Do nothing here
            }
            else
            {
                strcat(br0_ifnames, " ");
                strcat(br0_ifnames, if_name);
            }
            	
        }
     }

     for(i = MIN_BSSID_NUM; i <= MAX_BSSID_NUM; i++){
         sprintf(wl_param, "%s_%d", "wlg_sec_profile_enable", i);        
         if(nvram_match(wl_param, "1")){
             sprintf(if_name, "wl1.%d", i-1);
            if(nvram_match("enable_vlan", "enable"))
            {
                if(!(enabled_vlan_ports & IPTV_WLAN_GUEST2))
                {
                    strcat(br0_ifnames, " ");
                    strcat(br0_ifnames, if_name);
                }
            }
            else if (nvram_match(NVRAM_IPTV_ENABLED, "1"))
            {
            	// Do nothing here
            }
            else
            {
                strcat(br0_ifnames, " ");
                strcat(br0_ifnames, if_name);
            }
         }
     }
     nvram_set("lan_ifnames", br0_ifnames);
    /*added by dennis start, 05/03/2012,fixed guest network cannot reconnect issue*/
	/* Foxconn added start pling 08/17/2012 */
    /* Fix: When IPTV is enabled, WAN interface is "br1".
     * This can cause CTF/pktc to work abnormally.
     * So bypass CTF/pktc altogether */
    if (nvram_match(NVRAM_IPTV_ENABLED, "1"))
        eval("et", "robowr", "0xFFFF", "0xFB", "1");
    else
        eval("et", "robowr", "0xFFFF", "0xFB", "0");
    /* Foxconn added end pling 08/17/2012 */
    return 0;
}
#endif
#ifdef VLAN_SUPPORT

static int active_vlan(void)
{
    char buf[128];
    unsigned char mac[ETHER_ADDR_LEN];
    char eth0_mac[32];

    strcpy(eth0_mac, nvram_safe_get("et0macaddr"));
    ether_atoe(eth0_mac, mac);

    /* Set MAC address byte 0 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE0, mac[0]);
    system(buf);
    /* Set MAC address byte 1 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE1, mac[1]);
    system(buf);
    /* Set MAC address byte 2 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE2, mac[2]);
    system(buf);
    /* Set MAC address byte 3 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE3, mac[3]);
    system(buf);
    /* Set MAC address byte 4 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE4, mac[4]);
    system(buf);
    /* Set MAC address byte 5 */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X%02X", VCFG_PAGE, VCFG_REG, MAC_BYTE5, mac[5]);
    system(buf);
    /* Issue command to activate new vlan configuration. */
    sprintf(buf, "et robowr 0x%04X 0x%02X 0x%02X00", VCFG_PAGE, VCFG_REG, SET_VLAN);
    system(buf);

    return 0;
}
#endif

/* Foxconn Perry added start, 01/30/2015, for extender mode */
#ifdef CONFIG_EXTENDER_MODE

#define C_MAX_VAR_SIZE 256
static int idxStrToLen(char *idxStr)
{
    if (idxStr == 0)
        return 0;
    if (strcmp(idxStr,"1") ==0 )
        return 5; /* 40 bit = 5 * 8 */
    else if (strcmp(idxStr,"2") ==0 )
        return 13; /* 104 bit */
    else if (strcmp(idxStr,"3") ==0 )
        return 16; /* 128 bit */
    else
        return 0;
}


static int rc_ExtMode_configAP()
{     
    char tempVal[C_MAX_VAR_SIZE], val[C_MAX_VAR_SIZE];
    char securityType[C_MAX_VAR_SIZE], ssid[C_MAX_VAR_SIZE];
    char currentFlow[20];
    char Passphrase[C_MAX_VAR_SIZE];
    char apmode[C_MAX_VAR_SIZE];
    char sta_band[20] = {0};
    int  wepStatus; //0 - disable, 1 - 64bit, 2 - 128bit
    char pcKey[C_MAX_VAR_SIZE];
    char pcKeyLen[C_MAX_VAR_SIZE];
    char pcDefaultKey[C_MAX_VAR_SIZE];
    int  keylen, defaultKeyIdx;
    char tempStr[160], wl_same_sec[16];
    int disable_5g = 0;/* Foxconn added by Max Ding, 05/02/2013 */


    //strcpy(currentFlow, "WirelessSetting");
    strcpy(apmode, nvram_safe_get("ap_mode_cur"));
    strcpy(sta_band, nvram_safe_get("sta_band_cur"));

    /* 2.4G ssid */
    strcpy(tempVal, nvram_safe_get("wla_ssid_backup"));
    
    if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
    {
        nvram_set("wla_ssid_2", tempVal);
        nvram_set("wla_temp_ssid_2", tempVal);
    }
    else if(0 == strcmp(sta_band, "5G"))
    {
        nvram_set("wla_ssid", tempVal);
        nvram_set("wla_temp_ssid", tempVal);
    }
    
    if (!disable_5g)/* Foxconn added by Max Ding, 05/02/2013 */
    {
        /* 5G ssid */
        strcpy(tempVal, nvram_safe_get("wlg_ssid_backup"));
        if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
        {
            nvram_set("wlg_ssid_2", tempVal);
            nvram_set("wlg_temp_ssid_2", tempVal);
        }
        else if(0 == strcmp(sta_band, "2.4G"))
        {
            nvram_set("wlg_ssid", tempVal);
            nvram_set("wlg_temp_ssid", tempVal);
        }
    }
    

    /* security configuration */
    if (strcmp(apmode, "2") == 0) //dual band
    {               
        //get values from 2.4g div
        strcpy(securityType, nvram_safe_get("wla_secu_type_backup"));
        if (strcmp(securityType, "None") == 0)//None
        {
            /* security type: None*/
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_secu_type_2", "None");
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_secu_type", "None");
        }
        else if (strcmp(securityType, "WEP") == 0) //WEP
        {   
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_secu_type_2", "WEP");
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_secu_type", "WEP");
            
            strcpy(Passphrase, nvram_safe_get("wla_passphrase_backup"));
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_passphrase_2", Passphrase);
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_passphrase", Passphrase);
        
            /* get wep status, 0-disable, 1-64bit, 2-128bit */
            strcpy(tempVal, nvram_safe_get("wla_wep_length_backup"));
            wepStatus = atoi(tempVal);
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_wep_length_2",tempVal);
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_wep_length",tempVal);
            
            strcpy(pcKey, nvram_safe_get("wla_key1_backup"));
            if (strcmp(pcKey,"") != 0)
            {
                sprintf(pcKeyLen,"%d", wepStatus);
                if (0 != (keylen = idxStrToLen(pcKeyLen)))
                {
                    if (strlen(pcKey) != keylen*2)
                        printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                    else
                    {
                        if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                            nvram_set("wla_key1_2",pcKey);
                        else if(0 == strcmp(sta_band, "5G"))
                            nvram_set("wla_key1",pcKey);
                    }
                }
            }
            else
            {
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                    nvram_set("wla_key1_2","");
                else if(0 == strcmp(sta_band, "5G"))
                    nvram_set("wla_key1","");
            }
        
            strcpy(pcKey, nvram_safe_get("wla_key2_backup"));
            if (strcmp(pcKey,"") != 0)
            {
                sprintf(pcKeyLen,"%d", wepStatus);
                if (0 != (keylen = idxStrToLen(pcKeyLen)))
                {
                    if (strlen(pcKey) != keylen*2)
                        printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                    else
                    {
                        if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                            nvram_set("wla_key2_2",pcKey);
                        else if(0 == strcmp(sta_band, "5G"))
                            nvram_set("wla_key2",pcKey);
                    }
                }
            }
            else
            {
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                    nvram_set("wla_key2_2","");
                else if(0 == strcmp(sta_band, "5G"))
                    nvram_set("wla_key2","");
            }
        
            strcpy(pcKey, nvram_safe_get("wla_key3_backup"));
            if (strcmp(pcKey,"") != 0)
            {
                sprintf(pcKeyLen,"%d", wepStatus);
                if (0 != (keylen = idxStrToLen(pcKeyLen)))
                {
                    if (strlen(pcKey) != keylen*2)
                        printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                    else
                    {
                        if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                            nvram_set("wla_key3_2",pcKey);
                        else if(0 == strcmp(sta_band, "5G"))
                            nvram_set("wla_key3",pcKey);
                    }
                }
            }
            else
            {
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                    nvram_set("wla_key3_2","");
                else if(0 == strcmp(sta_band, "5G"))
                    nvram_set("wla_key3","");
            }
        
            strcpy(pcKey, nvram_safe_get("wla_key4_backup"));
            if (strcmp(pcKey,"") != 0)
            {
                sprintf(pcKeyLen,"%d", wepStatus);
                if (0 != (keylen = idxStrToLen(pcKeyLen)))
                {
                    if (strlen(pcKey) != keylen*2)
                        printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                    else
                    {
                        if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                            nvram_set("wla_key4_2",pcKey);
                        else if(0 == strcmp(sta_band, "5G"))
                            nvram_set("wla_key4",pcKey);
                    }
                }
            }
            else
            {
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                    nvram_set("wla_key4_2","");
                else if(0 == strcmp(sta_band, "5G"))
                    nvram_set("wla_key4","");
            }
            
            //I don't know if the following is needed??
            /* It's time to set default key */
            strcpy(pcDefaultKey, nvram_safe_get("wla_defaKey_backup"));
            if (pcDefaultKey != NULL)
                defaultKeyIdx = atoi(pcDefaultKey);
                
            sprintf(tempStr, "%d", (defaultKeyIdx-1));
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_defaKey_2",tempStr);
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_defaKey",tempStr);
        }
        else //PSK
        {
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_secu_type_2", securityType);
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_secu_type", securityType);
            
            strcpy(Passphrase, nvram_safe_get("wla_passphrase_backup"));
            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "2.4G")))
                nvram_set("wla_passphrase_2", Passphrase);
            else if(0 == strcmp(sta_band, "5G"))
                nvram_set("wla_passphrase", Passphrase);
        }
        
        if (!disable_5g)/* Foxconn added by Max Ding, 05/02/2013 */
        {
            //get values from 5g div
            strcpy(securityType, nvram_safe_get("wlg_secu_type_backup"));
            if (strcmp(securityType, "None") == 0)//None
            {
                /* security type: None*/
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_secu_type_2", "None");
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_secu_type", "None");
            }
            else if (strcmp(securityType, "WEP") == 0) //WEP
            {                                
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_secu_type_2", "WEP");
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_secu_type", "WEP");
                
                strcpy(Passphrase, nvram_safe_get("wlg_passphrase_backup"));
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_passphrase_2", Passphrase);
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_passphrase", Passphrase);
                
                /* get wep status, 0-disable, 1-64bit, 2-128bit */
                strcpy(tempVal, nvram_safe_get("wlg_wep_length_backup"));
                wepStatus = atoi(tempVal);
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_wep_length_2",tempVal);
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_wep_length",tempVal);
                
                strcpy(pcKey, nvram_safe_get("wlg_key1_backup"));
                if (strcmp(pcKey,"") != 0)
                {
                    sprintf(pcKeyLen,"%d", wepStatus);
                    if (0 != (keylen = idxStrToLen(pcKeyLen)))
                    {
                        if (strlen(pcKey) != keylen*2)
                            printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                        else
                        {
                            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                                nvram_set("wlg_key1_2",pcKey);
                            else if(0 == strcmp(sta_band, "2.4G"))
                                nvram_set("wlg_key1",pcKey);
                        }
                    }
                }
                else
                {
                    if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                        nvram_set("wlg_key1_2","");
                    else if(0 == strcmp(sta_band, "2.4G"))
                        nvram_set("wlg_key1","");
                }
            
                strcpy(pcKey, nvram_safe_get("wlg_key2_backup"));
                if (strcmp(pcKey,"") != 0)
                {
                    sprintf(pcKeyLen,"%d", wepStatus);
                    if (0 != (keylen = idxStrToLen(pcKeyLen)))
                    {
                        if (strlen(pcKey) != keylen*2)
                            printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                        else
                        {
                            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                                nvram_set("wlg_key2_2",pcKey);
                            else if(0 == strcmp(sta_band, "2.4G"))
                                nvram_set("wlg_key2",pcKey);
                        }
                    }
                }
                else
                {
                    if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                        nvram_set("wlg_key2_2","");
                    else if(0 == strcmp(sta_band, "2.4G"))
                        nvram_set("wlg_key2","");
                }
            
                strcpy(pcKey, nvram_safe_get("wlg_key3_backup"));
                if (strcmp(pcKey,"") != 0)
                {
                    sprintf(pcKeyLen,"%d", wepStatus);
                    if (0 != (keylen = idxStrToLen(pcKeyLen)))
                    {
                        if (strlen(pcKey) != keylen*2)
                            printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                        else
                        {
                            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                                nvram_set("wlg_key3_2",pcKey);
                            else if(0 == strcmp(sta_band, "2.4G"))
                                nvram_set("wlg_key3",pcKey);
                        }
                    }
                }
                else
                {
                    if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                        nvram_set("wlg_key3_2","");
                    else if(0 == strcmp(sta_band, "2.4G"))
                        nvram_set("wlg_key3","");
                }
            
                strcpy(pcKey, nvram_safe_get("wlg_key4_backup"));
                if (strcmp(pcKey,"") != 0)
                {
                    sprintf(pcKeyLen,"%d", wepStatus);
                    if (0 != (keylen = idxStrToLen(pcKeyLen)))
                    {
                        if (strlen(pcKey) != keylen*2)
                            printf("httpd error key=%s,keykeylen=%d\n", pcKey, strlen(pcKey));
                        else
                        {
                            if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                                nvram_set("wlg_key4_2",pcKey);
                            else if(0 == strcmp(sta_band, "2.4G"))
                                nvram_set("wlg_key4",pcKey);
                        }
                    }
                }
                else
                {
                    if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                        nvram_set("wlg_key4_2","");
                    else if(0 == strcmp(sta_band, "2.4G"))
                        nvram_set("wlg_key4","");
                }
                
                //I don't know if the following is needed??
                /* It's time to set default key */
                strcpy(pcDefaultKey, nvram_safe_get("wlg_defaKey_backup"));
                if (pcDefaultKey != NULL)
                    defaultKeyIdx = atoi(pcDefaultKey);
                    
                sprintf(tempStr, "%d", (defaultKeyIdx-1));
                
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_defaKey_2",tempStr);
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_defaKey",tempStr);
            }
            else //PSK
            {
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_secu_type_2", securityType);
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_secu_type", securityType);
                
                strcpy(Passphrase, nvram_safe_get("wlg_passphrase_backup"));
                if((0 == strcmp(sta_band, "both")) || (0 == strcmp(sta_band, "5G")))
                    nvram_set("wlg_passphrase_2", Passphrase);
                else if(0 == strcmp(sta_band, "2.4G"))
                    nvram_set("wlg_passphrase", Passphrase);
            }
        }
    }
}



void rc_backupWirelessParameter(){
        acosNvramConfig_set("wla_ssid_backup",acosNvramConfig_get("wla_ssid"));
        acosNvramConfig_set("wla_secu_type_backup",acosNvramConfig_get("wla_secu_type"));
        acosNvramConfig_set("wla_wep_length_backup",acosNvramConfig_get("wla_wep_length"));
        acosNvramConfig_set("wla_key1_backup",acosNvramConfig_get("wla_key1"));
        acosNvramConfig_set("wla_key2_backup",acosNvramConfig_get("wla_key2"));
        acosNvramConfig_set("wla_key3_backup",acosNvramConfig_get("wla_key3"));
        acosNvramConfig_set("wla_key4_backup",acosNvramConfig_get("wla_key4"));
        acosNvramConfig_set("wla_defaKey_backup",acosNvramConfig_get("wla_defaKey"));
        acosNvramConfig_set("wla_auth_type_backup",acosNvramConfig_get("wla_auth_type"));
        acosNvramConfig_set("wps_mixedmode_backup",acosNvramConfig_get("wps_mixedmode"));
        acosNvramConfig_set("wla_passphrase_backup",acosNvramConfig_get("wla_passphrase"));
        acosNvramConfig_set("wla_channel_backup",acosNvramConfig_get("wla_channel"));
        acosNvramConfig_set("wla_mode_backup",acosNvramConfig_get("wla_mode"));
        acosNvramConfig_set("wlg_ssid_backup",acosNvramConfig_get("wlg_ssid"));
        acosNvramConfig_set("wlg_secu_type_backup",acosNvramConfig_get("wlg_secu_type"));
        acosNvramConfig_set("wlg_wep_length_backup",acosNvramConfig_get("wlg_wep_length"));
        acosNvramConfig_set("wlg_key1_backup",acosNvramConfig_get("wlg_key1"));
        acosNvramConfig_set("wlg_key2_backup",acosNvramConfig_get("wlg_key2"));
        acosNvramConfig_set("wlg_key3_backup",acosNvramConfig_get("wlg_key3"));
        acosNvramConfig_set("wlg_key4_backup",acosNvramConfig_get("wlg_key4"));
        acosNvramConfig_set("wlg_defaKey_backup",acosNvramConfig_get("wlg_defaKey"));
        acosNvramConfig_set("wlg_auth_type_backup",acosNvramConfig_get("wlg_auth_type"));
        acosNvramConfig_set("wps_mixedmode_backup",acosNvramConfig_get("wps_mixedmode"));
        acosNvramConfig_set("wlg_passphrase_backup",acosNvramConfig_get("wlg_passphrase"));
        acosNvramConfig_set("wlg_channel_backup",acosNvramConfig_get("wlg_channel"));
        acosNvramConfig_set("wlg_mode_backup",acosNvramConfig_get("wlg_mode"));
}


void rc_backupGuestParameter(){
        nvram_set("wla_ssid_2_backup", nvram_safe_get("wla_ssid_2"));
        nvram_set("wla_secu_type_2_backup", nvram_safe_get("wla_secu_type_2"));
        nvram_set("wla_wep_length_2_backup", nvram_safe_get("wla_wep_length_2"));
        nvram_set("wla_key1_2_backup", nvram_safe_get("wla_key1_2"));
        nvram_set("wla_key2_2_backup", nvram_safe_get("wla_key2_2"));
        nvram_set("wla_key3_2_backup", nvram_safe_get("wla_key3_2"));
        nvram_set("wla_key4_2_backup", nvram_safe_get("wla_key4_2"));
        nvram_set("wla_defaKey_2_backup", nvram_safe_get("wla_defaKey_2"));
        nvram_set("wla_auth_type_2_backup", nvram_safe_get("wla_auth_type_2"));
        nvram_set("wps_temp_mixedmode_2_backup", nvram_safe_get("wps_temp_mixedmode_2"));
        nvram_set("wla_passphrase_2_backup", nvram_safe_get("wla_passphrase_2"));
        nvram_set("wla_channel_2_backup", nvram_safe_get("wla_channel_2"));
        nvram_set("wla_mode_2_backup", nvram_safe_get("wla_mode_2"));
        nvram_set("wlg_ssid_2_backup", nvram_safe_get("wlg_ssid_2"));
        nvram_set("wlg_secu_type_2_backup", nvram_safe_get("wlg_secu_type_2"));
        nvram_set("wlg_wep_length_2_backup", nvram_safe_get("wlg_wep_length_2"));
        nvram_set("wlg_key1_2_backup", nvram_safe_get("wlg_key1_2"));
        nvram_set("wlg_key2_2_backup", nvram_safe_get("wlg_key2_2"));
        nvram_set("wlg_key3_2_backup", nvram_safe_get("wlg_key3_2"));
        nvram_set("wlg_key4_2_backup", nvram_safe_get("wlg_key4_2"));
        nvram_set("wlg_defaKey_2_backup", nvram_safe_get("wlg_defaKey_2"));
        nvram_set("wlg_auth_type_2_backup", nvram_safe_get("wlg_auth_type_2"));
        nvram_set("wps_temp_mixedmode_2_backup", nvram_safe_get("wps_temp_mixedmode_2"));
        nvram_set("wlg_passphrase_2_backup", nvram_safe_get("wlg_passphrase_2"));
        nvram_set("wlg_channel_2_backup", nvram_safe_get("wlg_channel_2"));
        nvram_set("wlg_mode_2_backup", nvram_safe_get("wlg_temp_mode_2"));
}

static int config_extender_mode()
{
    if(nvram_match("TE_TEST", "1"))
        return 0;

    if(nvram_match("enable_sta_mode", "1"))
    {
        nvram_set("ap_mode_cur", "2");
        nvram_set("enable_extender_mode", "1");
        nvram_set("enable_sta_mode", "0");

        if(nvram_match("bridge_interface", "0"))
            nvram_set("sta_band_cur", "2.4G");
        else
            nvram_set("sta_band_cur", "5G");

        nvram_set("eth_bind_band", "5G");

        /* backup wifi settings */
        if(nvram_match("wla_ssid_backup", ""))
            rc_backupWirelessParameter();
        
        /* backup guest network settings */
        if(nvram_match("wla_ssid_2_backup", ""))
            rc_backupGuestParameter();
    
        /* config ap interface */        
        rc_ExtMode_configAP();
    }
    else if(nvram_match("wla_wds_enable", "1") && nvram_match("wla_wds_mode", "1") 
        && nvram_match("wla_repeater", "1"))
    {
        nvram_set("ap_mode_cur", "2");
        nvram_set("enable_extender_mode", "1");
        nvram_set("sta_band_cur", "2.4G");
        nvram_set("eth_bind_band", "2.4G");
        nvram_set("wla_wds_enable", "0");
        nvram_set("wla_repeater", "0");

        /* backup wifi settings */
        if(nvram_match("wla_ssid_backup", ""))
            rc_backupWirelessParameter();

        /* backup guest network settings */
        if(nvram_match("wla_ssid_2_backup", ""))
            rc_backupGuestParameter();

    
        /* config ap interface */        
        rc_ExtMode_configAP();

    }
    else if(nvram_match("wlg_wds_enable", "1") && nvram_match("wlg_wds_mode", "1") 
        && nvram_match("wlg_repeater", "1"))
    {
        nvram_set("ap_mode_cur", "2");
        nvram_set("enable_extender_mode", "1");
        nvram_set("sta_band_cur", "5G");
        nvram_set("eth_bind_band", "5G");
        nvram_set("wlg_wds_enable", "0");
        nvram_set("wlg_repeater", "0");

        /* backup wifi settings */
        if(nvram_match("wla_ssid_backup", ""))
            rc_backupWirelessParameter();

        /* backup guest network settings */
        if(nvram_match("wla_ssid_2_backup", ""))
            rc_backupGuestParameter();

    
        /* config ap interface */        
        rc_ExtMode_configAP();

    }

    return 0;
}

#endif /* CONFIG_EXTENDER_MODE */
/* Foxconn Perry added end, 01/30/2015, for extender mode */


#if (defined INCLUDE_QOS) || (defined __CONFIG_IGMP_SNOOPING__)
/* these settings are for BCM53115S switch */
static int config_switch_reg(void)
{


    if (
#if (defined __CONFIG_IGMP_SNOOPING__)
//        nvram_match("emf_enable", "1") ||
#endif
#if defined(CONFIG_RUSSIA_IPTV)
//		nvram_match("iptv_enabled", "1") ||
#endif          
		nvram_match("enable_vlan", "enable") ||
        (nvram_match("qos_enable", "1")  
        && !nvram_match("wla_repeater", "1")
#if (defined INCLUDE_DUAL_BAND)
        && !nvram_match("wlg_repeater", "1")
#endif
        && !nvram_match("qos_port", "")))
    {
        /* Enables the receipt of unicast, multicast and broadcast on IMP port */
        system("et robowr 0x00 0x08 0x1C");
        /* Enable Frame-managment mode */
        system("et robowr 0x00 0x0B 0x07");
        /* Enable management port */
        system("et robowr 0x02 0x00 0x80");
#ifdef BCM5301X           
        /*Enable BRCM header for port 5*/
        system("et robowr 0x02 0x03 0x02");  /* Foxconn Bob added for 4708 */
#endif        
        /* CRC bypass and auto generation */
        system("et robowr 0x34 0x06 0x11");
#if (defined __CONFIG_IGMP_SNOOPING__)
        if (nvram_match("emf_enable", "1"))
        {
            /* Set IMP port default tag id */
            system("et robowr 0x34 0x20 0x02");
            /* Enable IPMC bypass V fwdmap */
            system("et robowr 0x34 0x01 0x2E");
            /* Set Multiport address enable */
            system("et robowr 0x04 0x0E 0x0AAA");
        }
#endif
        /* Turn on the flags for kernel space (et/emf/igs) handling */
        system("et robowr 0xFFFF 0xFE 0x03");
    }
    else
    {
        system("et robowr 0x00 0x08 0x00");
        system("et robowr 0x00 0x0B 0x06");
        system("et robowr 0x02 0x00 0x00");
#ifdef BCM5301X          
        /*Enable BRCM header for port 8*/
        system("et robowr 0x02 0x03 0x01");  /* Foxconn Bob added for 4708 */
#endif        
        system("et robowr 0x34 0x06 0x10");
#if (defined __CONFIG_IGMP_SNOOPING__)
        system("et robowr 0x34 0x20 0x02");
        system("et robowr 0x34 0x01 0x0E");
        system("et robowr 0x04 0x0E 0x0000");
#endif
        if (nvram_match("qos_enable", "1"))
            system("et robowr 0xFFFF 0xFE 0x01");
        else if (!nvram_match("qos_port", ""))
            system("et robowr 0xFFFF 0xFE 0x02");
        else
            system("et robowr 0xFFFF 0xFE 0x00");
    }

    return 0;
}
/* foxconn added end, zacker, 01/13/2012, @iptv_igmp */

/* foxconn modified start, zacker, 01/13/2012, @iptv_igmp */
static void config_switch(void)
{
    /* BCM5325 & BCM53115 switch request to change these vars
     * to output ethernet port tag/id in packets.
     */
    struct nvram_tuple generic[] = {
        { "wan_ifname", "eth0", 0 },
        { "wan_ifnames", "eth0 ", 0 },
#if ( defined(R7000))
        { "vlan1ports", "1 2 3 4 5*", 0 },
        { "vlan2ports", "0 5u", 0 },
#else
        { "vlan1ports", "0 1 2 3 5*", 0 },
        { "vlan2ports", "4 5u", 0 },
#endif
        { 0, 0, 0 }
    };

    struct nvram_tuple vlan[] = {
        { "wan_ifname", "vlan2", 0 },
        { "wan_ifnames", "vlan2 ", 0 },
#if ( defined(R7000))
        { "vlan1ports", "1 2 3 4 5*", 0 },
        { "vlan2ports", "0 5", 0 },
#else
        { "vlan1ports", "0 1 2 3 5*", 0 },
        { "vlan2ports", "4 5", 0 },
#endif
        { 0, 0, 0 }
    };

        /* Foxconn Perry added start, 11/17/2014, for extender mode */
        /* Set wan interface to br0 for extender mode. */
#ifdef CONFIG_EXTENDER_MODE
    struct nvram_tuple ext_mode[] = {
        { "wan_ifname", "br0", 0 },
        { "wan_ifnames", "br0 ", 0 },
#if ( defined(R7000))
        { "vlan1ports", "1 2 3 4 5*", 0 },
        { "vlan2ports", "0 5", 0 },
#else
        { "vlan1ports", "0 1 2 3 5*", 0 },
        { "vlan2ports", "4 5", 0 },
#endif
        { 0, 0, 0 }
    };
#endif /* CONFIG_EXTENDER_MODE */
        /* Foxconn Perry added end, 11/17/2014, for extender mode */


    struct nvram_tuple *u = generic;
    int commit = 0;

    /* foxconn Bob modified start 08/26/2013, not to bridge eth0 and vlan1 in the same bridge */
    if (nvram_match("emf_enable", "1") || nvram_match("enable_ap_mode", "1") ) {
        u = vlan;
    }
    /* foxconn Bob modified end 08/26/2013, not to bridge eth0 and vlan1 in the same bridge */
    /* Foxconn Perry added start, 11/17/2014, for extender mode */
#ifdef CONFIG_EXTENDER_MODE
    else if(nvram_match("enable_extender_mode", "1"))
    {
        u = ext_mode;
    }
#endif /* CONFIG_EXTENDER_MODE */
    /* Foxconn Perry added end, 11/17/2014, for extender mode */



    /* don't need vlan in repeater mode */
    if (nvram_match("wla_repeater", "1")
#if (defined INCLUDE_DUAL_BAND)
        || nvram_match("wlg_repeater", "1")
#endif
        ) {
        u = generic;
    }

    for ( ; u && u->name; u++) {
        if (strcmp(nvram_safe_get(u->name), u->value)) {
            commit = 1;
            nvram_set(u->name, u->value);
        }
    }

    if (commit) {
        cprintf("Commit new ethernet config...\n");
        nvram_commit();
        commit = 0;
    }
}
#endif
/* foxconn modified end, zacker, 01/13/2012, @iptv_igmp */

/* foxconn modified start, zacker, 01/04/2011 */
static int should_stop_wps(void)
{
    /* WPS LED OFF */
    if ((nvram_match("wla_wlanstate","Disable") || acosNvramConfig_match("wifi_on_off", "0"))
#if (defined INCLUDE_DUAL_BAND)
        && (nvram_match("wlg_wlanstate","Disable") || acosNvramConfig_match("wifi_on_off", "0"))
#endif
       )
        return WPS_LED_STOP_RADIO_OFF;

    /* WPS LED quick blink for 5sec */
    if (nvram_match("wps_mode", "disabled")
        || nvram_match("wla_repeater", "1")
#if (defined INCLUDE_DUAL_BAND)
        || nvram_match("wlg_repeater", "1")
#endif
       )
        return WPS_LED_STOP_DISABLED;

    /* WPS LED original action */
    return WPS_LED_STOP_NO;
}

static int is_secure_wl(void)
{
    /* for ACR5500 , there is only on WiFi LED for WPS */
#if defined(R6300v2) || defined(R6250) || defined(R6200v2) || defined(R7000)

    if ((acosNvramConfig_match("wla_wlanstate","Disable") || acosNvramConfig_match("wifi_on_off", "0"))
        && (acosNvramConfig_match("wlg_wlanstate","Disable") || acosNvramConfig_match("wifi_on_off", "0")) )
        return 0;

    return 1;
#else    
    
    if (   (!acosNvramConfig_match("wla_secu_type", "None")
            && (acosNvramConfig_match("wla_wlanstate","Enable") && acosNvramConfig_match("wifi_on_off", "1")))
#if (defined INCLUDE_DUAL_BAND)
        || (!acosNvramConfig_match("wlg_secu_type", "None")
            && (acosNvramConfig_match("wlg_wlanstate","Enable") && acosNvramConfig_match("wifi_on_off", "1")))
#endif
        )
        return 1;

    return 0;
#endif /* defined(R6300v2) */    
}

/* Foxconn added start, Wins, 04/20/2011 @RU_IPTV */
#ifdef CONFIG_RUSSIA_IPTV
static int is_russia_specific_support (void)
{
    int result = 0;
    char sku_name[8];

    /* Router Spec v2.0:                                                        *
     *   Case 1: RU specific firmware.                                          *
     *   Case 2: single firmware & region code is RU.                           *
     *   Case 3: WW firmware & GUI language is Russian.                         *
     *   Case 4: single firmware & region code is WW & GUI language is Russian. *
     * Currently, new built firmware will be single firmware.                   */
    strcpy(sku_name, nvram_get("sku_name"));
    if (!strcmp(sku_name, "RU"))
    {
        /* Case 2: single firmware & region code is RU. */
        /* Region is RU (0x0005) */
        result = 1;
    }
    else if (!strcmp(sku_name, "WW"))
    {
        /* Region is WW (0x0002) */
        char gui_region[16];
        strcpy(gui_region, nvram_get("gui_region"));
        if (!strcmp(gui_region, "Russian"))
        {
            /* Case 4: single firmware & region code is WW & GUI language is Russian */
            /* GUI language is Russian */
            result = 1;
        }
    }

    return result;
}
#endif /* CONFIG_RUSSIA_IPTV */
/* Foxconn added end, Wins, 04/20/2011 @RU_IPTV */

/*Foxconn add start, edward zhang, 2013/07/03*/
#ifdef VLAN_SUPPORT
static int getVlanname(char vlanname[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE])
{
    char *var;

    if ((var = acosNvramConfig_get("vlan_name")) != NULL)
    {
        int num, i;
        num = getTokens(var, " ", vlanname, C_MAX_VLAN_RULE);
        for (i = 0; i< num; i++)
            restore_devname(vlanname[i]);
        
        return num;
    }

    return 0;
}



static int getVlanRule(vlan_rule vlan[C_MAX_VLAN_RULE])
{
    int numVlanRule = 0 , i;
    char *var;
    char VlanName[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE];
    char VlanId[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE];
    char VlanPrio[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE];
    char VlanPorts[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE];
    char VlanRuleEnable[C_MAX_VLAN_RULE][C_MAX_TOKEN_SIZE];
    if ( (var = acosNvramConfig_get("vlan_id")) != NULL )
    {
        getTokens(var, " ", VlanId, C_MAX_VLAN_RULE);
    }
    
    if ( (var=acosNvramConfig_get("vlan_prio")) != NULL )
    {
        getTokens(var, " ", VlanPrio, C_MAX_VLAN_RULE);
    }
    
    if ( (var=acosNvramConfig_get("vlan_ports")) != NULL )
    {
        getTokens(var, " ", VlanPorts, C_MAX_VLAN_RULE);
    }
 
    if ( (var=acosNvramConfig_get("vlan_rule_enable")) != NULL )
    {
        getTokens(var, " ", VlanRuleEnable, C_MAX_VLAN_RULE);
    }
    
    numVlanRule = getVlanname(VlanName);
    
    for(i=0;i<numVlanRule;i++)
    {
        strcpy( vlan[i].vlan_name , VlanName[i]);
        strcpy( vlan[i].vlan_id , VlanId[i]);
        strcpy( vlan[i].vlan_prio , VlanPrio[i]);
        //strcpy( vlan[i].vlan_ports , VlanPorts[i]);
        sprintf( vlan[i].vlan_ports,"%s",VlanPorts[i]);
        strcpy( vlan[i].enable_rule , VlanRuleEnable[i]);
    }
    
    return numVlanRule;
}
#endif
/* Foxconn add start, Edward zhang, 09/05/2012, @add IPTV support for PR SKU*/
static int is_china_specific_support (void)
{
    int result = 0;
    char sku_name[8];

    /* Router Spec v2.0:                                                        *
     *   Case 1: RU specific firmware.                                          *
     *   Case 2: single firmware & region code is PR.                           *
     *   Case 3: WW firmware & GUI language is Chinise.                         *
     *   Case 4: single firmware & region code is WW & GUI language is Chinise. *
     * Currently, new built firmware will be single firmware.                   */
    strcpy(sku_name, nvram_get("sku_name"));
    if (!strcmp(sku_name, "PR"))
    {
        /* Case 2: single firmware & region code is PR. */
        /* Region is PR (0x0004) */
        result = 1;
    }
    else if (!strcmp(sku_name, "WW"))
    {
        /* Region is WW (0x0002) */
        char gui_region[16];
        strcpy(gui_region, nvram_get("gui_region"));
        if (!strcmp(gui_region, "Chinese"))
        {
            /* Case 4: single firmware & region code is WW & GUI language is Chinise */
            /* GUI language is Chinise */
            result = 1;
        }
    }

    return result;
}
/* Foxconn add end, Edward zhang, 09/05/2012, @add IPTV support for PR SKU*/

static int send_wps_led_cmd(int cmd, int arg)
{
    int ret_val=0;
    int fd;
    int new_arg;

    fd = open(DEV_WPS_LED, O_RDWR);
    if (fd < 0) 
        return -1;

    if (is_secure_wl())
        new_arg = 1;
    else
        new_arg = 0;

    switch (should_stop_wps())
    {
        case WPS_LED_STOP_RADIO_OFF:
            cmd = WPS_LED_BLINK_OFF;
            break;
            
        case WPS_LED_STOP_DISABLED:
            if (cmd == WPS_LED_BLINK_NORMAL)
                cmd = WPS_LED_BLINK_QUICK;
            break;
            
        case WPS_LED_STOP_NO:
        default:
            break;
    }
    /* Foxconn modified start, Jesse Chen 12/05/2012 @match "send_wps_led_cmd" function in wps_monitor.c*/
    /* 
     *In case 3400v2, We can use "WPS_LED_CHANGE_GREEN" or "WPS_LED_CHANGE_AMBER" change wps led status,
     *need not to always follow it's wireless security settings
     */
#ifdef WNDR3400v2    
    if ( (cmd == WPS_LED_CHANGE_GREEN) || (cmd == WPS_LED_CHANGE_AMBER) )
         new_arg = arg;
#endif
    /* Foxconn modified end, Jesse Chen 12/05/2012 @match "send_wps_led_cmd" function in wps_monitor.c*/
    ret_val = ioctl(fd, cmd, new_arg);
    close(fd);

    return ret_val;
}
/* foxconn modified end, zacker, 01/04/2011 */

static int
build_ifnames(char *type, char *names, int *size)
{
	char name[32], *next;
	int len = 0;
	int s;

	/* open a raw scoket for ioctl */
	if ((s = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
		return -1;

	/*
	 * go thru all device names (wl<N> il<N> et<N> vlan<N>) and interfaces to
	 * build an interface name list in which each i/f name coresponds to a device
	 * name in device name list. Interface/device name matching rule is device
	 * type dependant:
	 *
	 *	wl:	by unit # provided by the driver, for example, if eth1 is wireless
	 *		i/f and its unit # is 0, then it will be in the i/f name list if
	 *		wl0 is in the device name list.
	 *	il/et:	by mac address, for example, if et0's mac address is identical to
	 *		that of eth2's, then eth2 will be in the i/f name list if et0 is
	 *		in the device name list.
	 *	vlan:	by name, for example, vlan0 will be in the i/f name list if vlan0
	 *		is in the device name list.
	 */
	foreach(name, type, next) {
		struct ifreq ifr;
		int i, unit;
		char var[32], *mac;
		unsigned char ea[ETHER_ADDR_LEN];

		/* vlan: add it to interface name list */
		if (!strncmp(name, "vlan", 4)) {
			/* append interface name to list */
			len += snprintf(&names[len], *size - len, "%s ", name);
			continue;
		}

		/* others: proceed only when rules are met */
		for (i = 1; i <= DEV_NUMIFS; i ++) {
			/* ignore i/f that is not ethernet */
			ifr.ifr_ifindex = i;
			if (ioctl(s, SIOCGIFNAME, &ifr))
				continue;
			if (ioctl(s, SIOCGIFHWADDR, &ifr))
				continue;
			if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)
				continue;
			if (!strncmp(ifr.ifr_name, "vlan", 4))
				continue;

			/* wl: use unit # to identify wl */
			if (!strncmp(name, "wl", 2)) {
				if (wl_probe(ifr.ifr_name) ||
				    wl_ioctl(ifr.ifr_name, WLC_GET_INSTANCE, &unit, sizeof(unit)) ||
				    unit != atoi(&name[2]))
					continue;
			}
			/* et/il: use mac addr to identify et/il */
			else if (!strncmp(name, "et", 2) || !strncmp(name, "il", 2)) {
				snprintf(var, sizeof(var), "%smacaddr", name);
				if (!(mac = nvram_get(var)) || !ether_atoe(mac, ea) ||
				    bcmp(ea, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN))
					continue;
			}
			/* mac address: compare value */
			else if (ether_atoe(name, ea) &&
				!bcmp(ea, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN))
				;
			/* others: ignore */
			else
				continue;

			/* append interface name to list */
			len += snprintf(&names[len], *size - len, "%s ", ifr.ifr_name);
		}
	}

	close(s);

	*size = len;
	return 0;
}

#ifdef __CONFIG_WSCCMD__
static void rc_randomKey()
{
	char random_ssid[33] = {0};
	unsigned char random_key[65] = {0};
	int i = 0;
	unsigned short key_length;

	RAND_bytes((unsigned char *)&key_length, sizeof(key_length));
	key_length = ((((long)key_length + 56791 )*13579)%8) + 8;

	/* random a ssid */
	sprintf(random_ssid, "Broadcom_");

	while (i < 6) {
		RAND_bytes(&random_ssid[9 + i], 1);
		if ((islower(random_ssid[9 + i]) || isdigit(random_ssid[9 + i])) && (random_ssid[9 + i] < 0x7f)) {
			i++;
		}
	}

	nvram_set("wl_ssid", random_ssid);

	i = 0;
	/* random a key */
	while (i < key_length) {
		RAND_bytes(&random_key[i], 1);
		if ((islower(random_key[i]) || isdigit(random_key[i])) && (random_key[i] < 0x7f)) {
			i++;
		}
	}
	random_key[key_length] = 0;

	nvram_set("wl_wpa_psk", random_key);

	/* Set default config to wps-psk, tkip */
	nvram_set("wl_akm", "psk ");
	nvram_set("wl_auth", "0");
	nvram_set("wl_wep", "disabled");
	nvram_set("wl_crypto", "tkip");

}
static void
wps_restore_defaults(void)
{
	/* cleanly up nvram for WPS */
	nvram_unset("wps_seed");
	nvram_unset("wps_config_state");
	nvram_unset("wps_addER");
	nvram_unset("wps_device_pin");
	nvram_unset("wps_pbc_force");
	nvram_unset("wps_config_command");
	nvram_unset("wps_proc_status");
	nvram_unset("wps_status");
	nvram_unset("wps_method");
	nvram_unset("wps_proc_mac");
	nvram_unset("wps_sta_pin");
	nvram_unset("wps_currentband");
	nvram_unset("wps_restart");
	nvram_unset("wps_event");

	nvram_unset("wps_enr_mode");
	nvram_unset("wps_enr_ifname");
	nvram_unset("wps_enr_ssid");
	nvram_unset("wps_enr_bssid");
	nvram_unset("wps_enr_wsec");

	nvram_unset("wps_unit");
}
#endif /* __CONFIG_WPS__ */

static void
virtual_radio_restore_defaults(void)
{
	char tmp[100], prefix[] = "wlXXXXXXXXXX_mssid_";
	int i, j;

	nvram_unset("unbridged_ifnames");
	nvram_unset("ure_disable");

	/* Delete dynamically generated variables */
	for (i = 0; i < MAX_NVPARSE; i++) {
		sprintf(prefix, "wl%d_", i);
		nvram_unset(strcat_r(prefix, "vifs", tmp));
		nvram_unset(strcat_r(prefix, "ssid", tmp));
		nvram_unset(strcat_r(prefix, "guest", tmp));
		nvram_unset(strcat_r(prefix, "ure", tmp));
		nvram_unset(strcat_r(prefix, "ipconfig_index", tmp));
		nvram_unset(strcat_r(prefix, "nas_dbg", tmp));
		sprintf(prefix, "lan%d_", i);
		nvram_unset(strcat_r(prefix, "ifname", tmp));
		nvram_unset(strcat_r(prefix, "ifnames", tmp));
		nvram_unset(strcat_r(prefix, "gateway", tmp));
		nvram_unset(strcat_r(prefix, "proto", tmp));
		nvram_unset(strcat_r(prefix, "ipaddr", tmp));
		nvram_unset(strcat_r(prefix, "netmask", tmp));
		nvram_unset(strcat_r(prefix, "lease", tmp));
		nvram_unset(strcat_r(prefix, "stp", tmp));
		nvram_unset(strcat_r(prefix, "hwaddr", tmp));
		sprintf(prefix, "dhcp%d_", i);
		nvram_unset(strcat_r(prefix, "start", tmp));
		nvram_unset(strcat_r(prefix, "end", tmp));

		/* clear virtual versions */
		for (j = 0; j < 16; j++) {
			sprintf(prefix, "wl%d.%d_", i, j);
			nvram_unset(strcat_r(prefix, "ssid", tmp));
			nvram_unset(strcat_r(prefix, "ipconfig_index", tmp));
			nvram_unset(strcat_r(prefix, "guest", tmp));
			nvram_unset(strcat_r(prefix, "closed", tmp));
			nvram_unset(strcat_r(prefix, "wpa_psk", tmp));
			nvram_unset(strcat_r(prefix, "auth", tmp));
			nvram_unset(strcat_r(prefix, "wep", tmp));
			nvram_unset(strcat_r(prefix, "auth_mode", tmp));
			nvram_unset(strcat_r(prefix, "crypto", tmp));
			nvram_unset(strcat_r(prefix, "akm", tmp));
			nvram_unset(strcat_r(prefix, "hwaddr", tmp));
			nvram_unset(strcat_r(prefix, "bss_enabled", tmp));
			nvram_unset(strcat_r(prefix, "bss_maxassoc", tmp));
			nvram_unset(strcat_r(prefix, "wme_bss_disable", tmp));
			nvram_unset(strcat_r(prefix, "ifname", tmp));
			nvram_unset(strcat_r(prefix, "unit", tmp));
			nvram_unset(strcat_r(prefix, "ap_isolate", tmp));
			nvram_unset(strcat_r(prefix, "macmode", tmp));
			nvram_unset(strcat_r(prefix, "maclist", tmp));
			nvram_unset(strcat_r(prefix, "maxassoc", tmp));
			nvram_unset(strcat_r(prefix, "mode", tmp));
			nvram_unset(strcat_r(prefix, "radio", tmp));
			nvram_unset(strcat_r(prefix, "radius_ipaddr", tmp));
			nvram_unset(strcat_r(prefix, "radius_port", tmp));
			nvram_unset(strcat_r(prefix, "radius_key", tmp));
			nvram_unset(strcat_r(prefix, "key", tmp));
			nvram_unset(strcat_r(prefix, "key1", tmp));
			nvram_unset(strcat_r(prefix, "key2", tmp));
			nvram_unset(strcat_r(prefix, "key3", tmp));
			nvram_unset(strcat_r(prefix, "key4", tmp));
			nvram_unset(strcat_r(prefix, "wpa_gtk_rekey", tmp));
			nvram_unset(strcat_r(prefix, "nas_dbg", tmp));
		}
	}
}

#ifdef __CONFIG_NAT__
static void
auto_bridge(void)
{

	struct nvram_tuple generic[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "eth0 eth2 eth3 eth4", 0 },
		{ "wan_ifname", "eth1", 0 },
		{ "wan_ifnames", "eth1", 0 },
		{ 0, 0, 0 }
	};
#ifdef __CONFIG_VLAN__
	struct nvram_tuple vlan[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "vlan0 eth1 eth2 eth3", 0 },
		{ "wan_ifname", "vlan1", 0 },
		{ "wan_ifnames", "vlan1", 0 },
		{ 0, 0, 0 }
	};
#endif	/* __CONFIG_VLAN__ */
	struct nvram_tuple dyna[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "", 0 },
		{ "wan_ifname", "", 0 },
		{ "wan_ifnames", "", 0 },
		{ 0, 0, 0 }
	};
	struct nvram_tuple generic_auto_bridge[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "eth0 eth1 eth2 eth3 eth4", 0 },
		{ "wan_ifname", "", 0 },
		{ "wan_ifnames", "", 0 },
		{ 0, 0, 0 }
	};
#ifdef __CONFIG_VLAN__
	struct nvram_tuple vlan_auto_bridge[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "vlan0 vlan1 eth1 eth2 eth3", 0 },
		{ "wan_ifname", "", 0 },
		{ "wan_ifnames", "", 0 },
		{ 0, 0, 0 }
	};
#endif	/* __CONFIG_VLAN__ */

	struct nvram_tuple dyna_auto_bridge[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "", 0 },
		{ "wan_ifname", "", 0 },
		{ "wan_ifnames", "", 0 },
		{ 0, 0, 0 }
	};

	struct nvram_tuple *linux_overrides;
	struct nvram_tuple *t, *u;
	int auto_bridge = 0, i;
#ifdef __CONFIG_VLAN__
	uint boardflags;
#endif	/* __CONFIG_VLAN_ */
	char *landevs, *wandevs;
	char lan_ifnames[128], wan_ifnames[128];
	char dyna_auto_ifnames[128];
	char wan_ifname[32], *next;
	int len;
	int ap = 0;

	printf(" INFO : enter function auto_bridge()\n");

	if (!strcmp(nvram_safe_get("auto_bridge_action"), "1")) {
		auto_bridge = 1;
		cprintf("INFO: Start auto bridge...\n");
	} else {
		nvram_set("router_disable_auto", "0");
		cprintf("INFO: Start non auto_bridge...\n");
	}

	/* Delete dynamically generated variables */
	if (auto_bridge) {
		char tmp[100], prefix[] = "wlXXXXXXXXXX_";
		for (i = 0; i < MAX_NVPARSE; i++) {

			del_filter_client(i);
			del_forward_port(i);
#if !defined(AUTOFW_PORT_DEPRECATED)
			del_autofw_port(i);
#endif

			snprintf(prefix, sizeof(prefix), "wan%d_", i);
			for (t = router_defaults; t->name; t ++) {
				if (!strncmp(t->name, "wan_", 4))
					nvram_unset(strcat_r(prefix, &t->name[4], tmp));
			}
		}
	}

	/*
	 * Build bridged i/f name list and wan i/f name list from lan device name list
	 * and wan device name list. Both lan device list "landevs" and wan device list
	 * "wandevs" must exist in order to preceed.
	 */
	if ((landevs = nvram_get("landevs")) && (wandevs = nvram_get("wandevs"))) {
		/* build bridged i/f list based on nvram variable "landevs" */
		len = sizeof(lan_ifnames);
		if (!build_ifnames(landevs, lan_ifnames, &len) && len)
			dyna[1].value = lan_ifnames;
		else
			goto canned_config;
		/* build wan i/f list based on nvram variable "wandevs" */
		len = sizeof(wan_ifnames);
		if (!build_ifnames(wandevs, wan_ifnames, &len) && len) {
			dyna[3].value = wan_ifnames;
			foreach(wan_ifname, wan_ifnames, next) {
				dyna[2].value = wan_ifname;
				break;
			}
		}
		else
			ap = 1;

		if (auto_bridge)
		{
			printf("INFO: lan_ifnames=%s\n", lan_ifnames);
			printf("INFO: wan_ifnames=%s\n", wan_ifnames);
			sprintf(dyna_auto_ifnames, "%s %s", lan_ifnames, wan_ifnames);
			printf("INFO: dyna_auto_ifnames=%s\n", dyna_auto_ifnames);
			dyna_auto_bridge[1].value = dyna_auto_ifnames;
			linux_overrides = dyna_auto_bridge;
			printf("INFO: linux_overrides=dyna_auto_bridge \n");
		}
		else
		{
			linux_overrides = dyna;
			printf("INFO: linux_overrides=dyna \n");
		}

	}
	/* override lan i/f name list and wan i/f name list with default values */
	else {
canned_config:
#ifdef __CONFIG_VLAN__
		boardflags = strtoul(nvram_safe_get("boardflags"), NULL, 0);
		if (boardflags & BFL_ENETVLAN) {
			if (auto_bridge)
			{
				linux_overrides = vlan_auto_bridge;
				printf("INFO: linux_overrides=vlan_auto_bridge \n");
			}
			else
			{
				linux_overrides = vlan;
				printf("INFO: linux_overrides=vlan \n");
			}
		} else {
#endif	/* __CONFIG_VLAN__ */
			if (auto_bridge)
			{
				linux_overrides = generic_auto_bridge;
				printf("INFO: linux_overrides=generic_auto_bridge \n");
			}
			else
			{
				linux_overrides = generic;
				printf("INFO: linux_overrides=generic \n");
			}
#ifdef __CONFIG_VLAN__
		}
#endif	/* __CONFIG_VLAN__ */
	}

		for (u = linux_overrides; u && u->name; u++) {
			nvram_set(u->name, u->value);
			printf("INFO: action nvram_set %s, %s\n", u->name, u->value);
			}

	/* Force to AP */
	if (ap)
		nvram_set("router_disable", "1");

	if (auto_bridge) {
		printf("INFO: reset auto_bridge flag.\n");
		nvram_set("auto_bridge_action", "0");
	}

	nvram_commit();
	cprintf("auto_bridge done\n");
}

#endif	/* __CONFIG_NAT__ */


static void
upgrade_defaults(void)
{
	char temp[100];
	int i;
	bool bss_enabled = TRUE;
	char *val;

	/* Check whether upgrade is required or not
	 * If lan1_ifnames is not found in NVRAM , upgrade is required.
	 */
	if (!nvram_get("lan1_ifnames") && !RESTORE_DEFAULTS()) {
		cprintf("NVRAM upgrade required.  Starting.\n");

		if (nvram_match("ure_disable", "1")) {
			nvram_set("lan1_ifname", "br1");
			nvram_set("lan1_ifnames", "wl0.1 wl0.2 wl0.3 wl1.1 wl1.2 wl1.3");
		}
		else {
			nvram_set("lan1_ifname", "");
			nvram_set("lan1_ifnames", "");
			for (i = 0; i < 2; i++) {
				snprintf(temp, sizeof(temp), "wl%d_ure", i);
				if (nvram_match(temp, "1")) {
					snprintf(temp, sizeof(temp), "wl%d.1_bss_enabled", i);
					nvram_set(temp, "1");
				}
				else {
					bss_enabled = FALSE;
					snprintf(temp, sizeof(temp), "wl%d.1_bss_enabled", i);
					nvram_set(temp, "0");
				}
			}
		}
		if (nvram_get("lan1_ipaddr")) {
			nvram_set("lan1_gateway", nvram_get("lan1_ipaddr"));
		}

		for (i = 0; i < 2; i++) {
			snprintf(temp, sizeof(temp), "wl%d_bss_enabled", i);
			nvram_set(temp, "1");
			snprintf(temp, sizeof(temp), "wl%d.1_guest", i);
			if (nvram_match(temp, "1")) {
				nvram_unset(temp);
				if (bss_enabled) {
					snprintf(temp, sizeof(temp), "wl%d.1_bss_enabled", i);
					nvram_set(temp, "1");
				}
			}

			snprintf(temp, sizeof(temp), "wl%d.1_net_reauth", i);
			val = nvram_get(temp);
			if (!val || (*val == 0))
				nvram_set(temp, nvram_default_get(temp));

			snprintf(temp, sizeof(temp), "wl%d.1_wpa_gtk_rekey", i);
			val = nvram_get(temp);
			if (!val || (*val == 0))
				nvram_set(temp, nvram_default_get(temp));
		}

		nvram_commit();

		cprintf("NVRAM upgrade complete.\n");
	}
}

static void
restore_defaults(void)
{
#if 0 /* foxconn wklin removed start, 10/22/2008 */
	struct nvram_tuple generic[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "eth0 eth2 eth3 eth4", 0 },
		{ "wan_ifname", "eth1", 0 },
		{ "wan_ifnames", "eth1", 0 },
		{ "lan1_ifname", "br1", 0 },
		{ "lan1_ifnames", "wl0.1 wl0.2 wl0.3 wl1.1 wl1.2 wl1.3", 0 },
		{ 0, 0, 0 }
	};
#ifdef __CONFIG_VLAN__
	struct nvram_tuple vlan[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "vlan0 eth1 eth2 eth3", 0 },
		{ "wan_ifname", "vlan1", 0 },
		{ "wan_ifnames", "vlan1", 0 },
		{ "lan1_ifname", "br1", 0 },
		{ "lan1_ifnames", "wl0.1 wl0.2 wl0.3 wl1.1 wl1.2 wl1.3", 0 },
		{ 0, 0, 0 }
	};
#endif	/* __CONFIG_VLAN__ */
	struct nvram_tuple dyna[] = {
		{ "lan_ifname", "br0", 0 },
		{ "lan_ifnames", "", 0 },
		{ "wan_ifname", "", 0 },
		{ "wan_ifnames", "", 0 },
		{ "lan1_ifname", "br1", 0 },
		{ "lan1_ifnames", "wl0.1 wl0.2 wl0.3 wl1.1 wl1.2 wl1.3", 0 },
		{ 0, 0, 0 }
	};
#endif /* 0 */ /* foxconn wklin removed end, 10/22/2008 */

	/* struct nvram_tuple *linux_overrides; *//* foxconn wklin removed, 10/22/2008 */
	struct nvram_tuple *t, *u;
	int restore_defaults, i;
#ifdef __CONFIG_VLAN__
	uint boardflags;
#endif	/* __CONFIG_VLAN_ */ 
    /* foxconn  wklin removed start, 10/22/2008*/
    /*
	char *landevs, *wandevs;
	char lan_ifnames[128], wan_ifnames[128];
	char wan_ifname[32], *next;
	int len;
	int ap = 0;
    */
    /* foxconn wklin removed end, 10/22/2008 */
#ifdef TRAFFIC_MGMT
	int j;
#endif  /* TRAFFIC_MGMT */

	/* Restore defaults if told to or OS has changed */
	restore_defaults = RESTORE_DEFAULTS();

	if (restore_defaults)
		cprintf("Restoring defaults...");

	/* Delete dynamically generated variables */
	if (restore_defaults) {
		char tmp[100], prefix[] = "wlXXXXXXXXXX_";
		for (i = 0; i < MAX_NVPARSE; i++) {
#ifdef __CONFIG_NAT__
			del_filter_client(i);
			del_forward_port(i);
#if !defined(AUTOFW_PORT_DEPRECATED)
			del_autofw_port(i);
#endif
#endif	/* __CONFIG_NAT__ */
			snprintf(prefix, sizeof(prefix), "wl%d_", i);
			for (t = router_defaults; t->name; t ++) {
				if (!strncmp(t->name, "wl_", 3))
					nvram_unset(strcat_r(prefix, &t->name[3], tmp));
			}
#ifdef __CONFIG_NAT__
			snprintf(prefix, sizeof(prefix), "wan%d_", i);
			for (t = router_defaults; t->name; t ++) {
				if (!strncmp(t->name, "wan_", 4))
					nvram_unset(strcat_r(prefix, &t->name[4], tmp));
			}
#endif	/* __CONFIG_NAT__ */
#ifdef TRAFFIC_MGMT
			snprintf(prefix, sizeof(prefix), "wl%d_", i);
			for (j = 0; j < MAX_NUM_TRF_MGMT_RULES; j++) {
				del_trf_mgmt_port(prefix, j);
			}
#endif  /* TRAFFIC_MGMT */
		}
#ifdef __CONFIG_WSCCMD__
		wps_restore_defaults();
#endif /* __CONFIG_WSCCMD__ */
#ifdef __CONFIG_WAPI_IAS__
		nvram_unset("as_mode");
#endif /* __CONFIG_WAPI_IAS__ */

		virtual_radio_restore_defaults();
	}

#if 0 /* foxconn removed start, wklin, 10/22/2008, we don't need this */
	/* 
	 * Build bridged i/f name list and wan i/f name list from lan device name list
	 * and wan device name list. Both lan device list "landevs" and wan device list
	 * "wandevs" must exist in order to preceed.
	 */
	if ((landevs = nvram_get("landevs")) && (wandevs = nvram_get("wandevs"))) {
		/* build bridged i/f list based on nvram variable "landevs" */
		len = sizeof(lan_ifnames);
		if (!build_ifnames(landevs, lan_ifnames, &len) && len)
			dyna[1].value = lan_ifnames;
		else
			goto canned_config;
		/* build wan i/f list based on nvram variable "wandevs" */
		len = sizeof(wan_ifnames);
		if (!build_ifnames(wandevs, wan_ifnames, &len) && len) {
			dyna[3].value = wan_ifnames;
			foreach(wan_ifname, wan_ifnames, next) {
				dyna[2].value = wan_ifname;
				break;
			}
		}
		else
			ap = 1;
		linux_overrides = dyna;
	}
	/* override lan i/f name list and wan i/f name list with default values */
	else {
canned_config:
#ifdef __CONFIG_VLAN__
		boardflags = strtoul(nvram_safe_get("boardflags"), NULL, 0);
		if (boardflags & BFL_ENETVLAN)
			linux_overrides = vlan;
		else
#endif	/* __CONFIG_VLAN__ */
			linux_overrides = generic;
	}

	/* Check if nvram version is set, but old */
	if (nvram_get("nvram_version")) {
		int old_ver, new_ver;

		old_ver = atoi(nvram_get("nvram_version"));
		new_ver = atoi(NVRAM_SOFTWARE_VERSION);
		if (old_ver < new_ver) {
			cprintf("NVRAM: Updating from %d to %d\n", old_ver, new_ver);
			nvram_set("nvram_version", NVRAM_SOFTWARE_VERSION);
		}
	}
#endif /* 0 */ /* foxconn removed end, wklin, 10/22/2008 */

	/* Restore defaults */
	for (t = router_defaults; t->name; t++) {
		if (restore_defaults || !nvram_get(t->name)) {
#if 0 /* foxconn removed, wklin, 10/22/2008 , no overrides */
			for (u = linux_overrides; u && u->name; u++) {
				if (!strcmp(t->name, u->name)) {
					nvram_set(u->name, u->value);
					break;
				}
			}
			if (!u || !u->name)
#endif
			nvram_set(t->name, t->value);
			//if(nvram_get(t->name))
			//	cprintf("%s:%s\n", t->name, nvram_get(t->name));
		}
	}

#ifdef __CONFIG_WSCCMD__
	//rc_randomKey();
#endif
	/* Force to AP */
#if 0 /* foxconn wklin removed, 10/22/2008 */
	if (ap)
		nvram_set("router_disable", "1");
#endif

	/* Always set OS defaults */
	nvram_set("os_name", "linux");
	nvram_set("os_version", ROUTER_VERSION_STR);
	nvram_set("os_date", __DATE__);
	/* Always set WL driver version! */
	nvram_set("wl_version", EPI_VERSION_STR);

	nvram_set("is_modified", "0");
	nvram_set("ezc_version", EZC_VERSION_STR);

	if (restore_defaults) {
	    
	    /* Foxconn removed start, Tony W.Y. Wang, 04/06/2010 */
#if 0
	    /* Foxconn add start by aspen Bai, 02/12/2009 */
		nvram_unset("pa2gw0a0");
		nvram_unset("pa2gw1a0");
		nvram_unset("pa2gw2a0");
		nvram_unset("pa2gw0a1");
		nvram_unset("pa2gw1a1");
		nvram_unset("pa2gw2a1");
#ifdef FW_VERSION_NA
		acosNvramConfig_setPAParam(0);
#else
		acosNvramConfig_setPAParam(1);
#endif
		/* Foxconn add end by aspen Bai, 02/12/2009 */
#endif
		/* Foxconn removed end, Tony W.Y. Wang, 04/06/2010 */
		
		/* foxconn modified start, zacker, 08/06/2010 */
		/* Create a new value to inform loaddefault in "read_bd" */
		nvram_set("load_defaults", "1");
		/* foxconn added start, antony, 03/31/2015 */
      nvram_set("qos_db_reset", "1");
		/* foxconn added end, antony, 03/31/2015 */

        eval("read_bd"); /* foxconn wklin added, 10/22/2008 */
		/* finished "read_bd", unset load_defaults flag */
		nvram_unset("load_defaults");
		/* foxconn modified end, zacker, 08/06/2010 */
        /* Foxconn add start, Tony W.Y. Wang, 04/06/2010 */
#ifdef SINGLE_FIRMWARE
        if (nvram_match("sku_name", "NA"))
            acosNvramConfig_setPAParam(0);
        else
            acosNvramConfig_setPAParam(1);
#else
		#ifdef FW_VERSION_NA
			acosNvramConfig_setPAParam(0);
		#else
			acosNvramConfig_setPAParam(1);
		#endif
#endif
        /* Foxconn add end, Tony W.Y. Wang, 04/06/2010 */
		nvram_commit();
		sync();         /* Foxconn added start pling 12/25/2006 */
		cprintf("done\n");
	}
}

#ifdef __CONFIG_NAT__
static void
set_wan0_vars(void)
{
	int unit;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";

	/* check if there are any connections configured */
	for (unit = 0; unit < MAX_NVPARSE; unit ++) {
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);
		if (nvram_get(strcat_r(prefix, "unit", tmp)))
			break;
	}
	/* automatically configure wan0_ if no connections found */
	if (unit >= MAX_NVPARSE) {
		struct nvram_tuple *t;
		char *v;

		/* Write through to wan0_ variable set */
		snprintf(prefix, sizeof(prefix), "wan%d_", 0);
		for (t = router_defaults; t->name; t ++) {
			if (!strncmp(t->name, "wan_", 4)) {
				if (nvram_get(strcat_r(prefix, &t->name[4], tmp)))
					continue;
				v = nvram_get(t->name);
				nvram_set(tmp, v ? v : t->value);
			}
		}
		nvram_set(strcat_r(prefix, "unit", tmp), "0");
		nvram_set(strcat_r(prefix, "desc", tmp), "Default Connection");
		nvram_set(strcat_r(prefix, "primary", tmp), "1");
	}
}
#endif	/* __CONFIG_NAT__ */

static int noconsole = 0;

static void
sysinit(void)
{
	char buf[PATH_MAX];
	struct utsname name;
	struct stat tmp_stat;
	time_t tm = 0;
	char *loglevel;

	struct utsname unamebuf;
	char *lx_rel;

	/* Use uname() to get the system's hostname */
	uname(&unamebuf);
	lx_rel = unamebuf.release;

	if (memcmp(lx_rel, "2.6", 3) == 0) {
		int fd;
		if ((fd = open("/dev/console", O_RDWR)) < 0) {
			if (memcmp(lx_rel, "2.6.36", 6) == 0) {
				mount("devfs", "/dev", "devtmpfs", MS_MGC_VAL, NULL);
			} else {
				mount("devfs", "/dev", "tmpfs", MS_MGC_VAL, NULL);
				mknod("/dev/console", S_IRWXU|S_IFCHR, makedev(5, 1));
			}
		}
		else {
			close(fd);
		}
	}

	/* /proc */
	mount("proc", "/proc", "proc", MS_MGC_VAL, NULL);
#ifdef LINUX26
	mount("sysfs", "/sys", "sysfs", MS_MGC_VAL, NULL);
#endif /* LINUX26 */

	/* /tmp */
	mount("ramfs", "/tmp", "ramfs", MS_MGC_VAL, NULL);

	/* /var */
	mkdir("/tmp/var", 0777);
	mkdir("/var/lock", 0777);
	mkdir("/var/log", 0777);
	mkdir("/var/run", 0777);
	mkdir("/var/tmp", 0777);
	mkdir("/tmp/media", 0777);
	/* Foxconn added start by Kathy, 10/14/2013 @ Facebook WiFi */
    mkdir("/tmp/fbwifi", 0777);
	/* Foxconn added end by Kathy, 10/14/2013 @ Facebook WiFi */

#ifdef __CONFIG_UTELNETD__
	/* If kernel enable unix908 pty then we have to make following things. */
	mkdir("/dev/pts", 0777);
	if (mount("devpts", "/dev/pts", "devpts", MS_MGC_VAL, NULL) == 0) {
		/* pty master */
		mknod("/dev/ptmx", S_IRWXU|S_IFCHR, makedev(5, 2));
	} else {
		rmdir("/dev/pts");
	}
#endif	/* LINUX2636 && __CONFIG_UTELNETD__ */

#ifdef __CONFIG_SAMBA__
	/* Add Samba Stuff */
	mkdir("/tmp/samba", 0777);
	mkdir("/tmp/samba/lib", 0777);
	mkdir("/tmp/samba/private", 0777);
	mkdir("/tmp/samba/var", 0777);
	mkdir("/tmp/samba/var/locks", 0777);

#if defined(LINUX_2_6_36)
	/* To improve SAMBA upload performance */
	reclaim_mem_earlier();
#endif /* LINUX_2_6_36 */
#endif

#ifdef BCMQOS
	mkdir("/tmp/qos", 0777);
#endif
	/* Setup console */
	if (console_init())
		noconsole = 1;

#ifdef LINUX26
	mkdir("/dev/shm", 0777);
	eval("/sbin/hotplug2", "--coldplug");
#endif /* LINUX26 */

	if ((loglevel = nvram_get("console_loglevel")))
		klogctl(8, NULL, atoi(loglevel));
	else
		klogctl(8, NULL, 1);

	/* Modules */
	uname(&name);
	snprintf(buf, sizeof(buf), "/lib/modules/%s", name.release);
	if (stat("/proc/modules", &tmp_stat) == 0 &&
	    stat(buf, &tmp_stat) == 0) {
		char module[80], *modules, *next;

		/* foxconn modified start, zacker, 08/06/2010 */
		/* Restore defaults if necessary */
  nvram_set ("wireless_restart", "1");
		restore_defaults();


        /* For 4500 IR-159. by MJ. 2011.07.04  */
        /* Foxconn added start pling 02/11/2011 */
        /* WNDR4000 IR20: unset vifs NVRAM and let
         * bcm_wlan_util.c to reconstruct them if
         * necessary. move to here since they should be
         * done before read_bd */
        nvram_unset("wl0_vifs");
        nvram_unset("wl1_vifs");
        /* Foxconn added end pling 02/11/2011 */
        /* Foxconn added start,James Hsu, 03/26/2015 # arlo interface BRCM nvram setting */
#ifdef ARLO_SUPPORT
        nvram_set("wl0.2_mbss_setting_override","1");
        nvram_set("wl0.2_bss_block_multicast","1");
        nvram_set("wl0.2_bcn","25");
        nvram_set("wl0.2_dtim","41");
        nvram_set("wl0.2_srl","15");
        nvram_set("wl0.2_lrl","15");
        nvram_set("wl0.2_force_bcn_rspec","4");
        nvram_set("wl0.2_rateset","0x3f");
        nvram_set("wl0.2_ampdu_rts","0");
        nvram_set("wl0.2_ampdu_mpdu","8");
        //nvram_set("wl0_taf_enable","1");
        //nvram_set("toad_ifnames","eth1");
        nvram_set("wl0.2_ampdu_ini_dead_timedout","5");
        nvram_set("wl0.2_scb_activity_time","10");
        nvram_set("wl0.2_psq_ageing_time","0");
        nvram_set("wl0.2_txbf_virtif_disable","1");
        system("cat /proc/meminfo >/tmp/meminfo.txt");
        FILE *fp;
        char buf[512];
        char size[64],arlo_ssid[32];
        int i,checksum=0;
        fp=fopen("/tmp/meminfo.txt","r");
        if(fp == NULL)
        {
            printf("Can't open file.");
        }
        else
        {
            for (i=0;i<10;i++)
            {
                fgets(buf, sizeof(buf), fp);
                sscanf(buf,"%*s%s%*s",size);
                //printf("%s\n",size);
                if( i == 1 || i == 3 || i == 9 )
                    checksum=checksum+atoi(size);
            }
        }
        if(acosNvramConfig_match("wla_ssid_3",""))
        {
            sprintf(arlo_ssid,"NTGR_R7000_Arlo_%d",checksum);
            printf("******** arlo_ssid %s **********\n",arlo_ssid);
            acosNvramConfig_set("wla_ssid_3",arlo_ssid);
        }
        if(acosNvramConfig_match("wla_sec_profile_enable_3","0"))
            acosNvramConfig_set("wla_sec_profile_enable_3", "1");
        if(acosNvramConfig_match("wla_passphrase_3",""))
            acosNvramConfig_set("wla_passphrase_3", "12345678");
        if(acosNvramConfig_match("wla_secu_type_3","None"))
            acosNvramConfig_set("wla_secu_type_3", "WPA2-PSK");
        if(acosNvramConfig_match("x_handler_event_sink",""))
            acosNvramConfig_set("x_handler_event_sink", "127.0.0.1:4002");
        if(acosNvramConfig_match("x_handler_1002",""))
            acosNvramConfig_set("x_handler_1002", "127.0.0.1:4001");
#endif
        nvram_set("wl0.2_dhcp_unicast","0");

        /* Foxconn added end,James Hsu, 03/26/2015 */
        /*Foxconn lawrence added start, 2013/03/06, Restore wifi_on_off button for default*/
	//nvram_set("wifi_on_off", "1"); Tab Tseng removed, 2014/02/27
        /*Foxconn lawrence added end, 2013/03/06, Restore wifi_on_off button for default*/
		
        /* Read ethernet MAC, RF params, etc */
		eval("read_bd");
        /* foxconn modified end, zacker, 08/06/2010 */

		/* Load ctf */


		/* Foxconn added start pling 06/26/2014 */
		/* Change CTF mode when access control is enabled */
		if (nvram_match("access_control_mode", "1") &&
			!nvram_match("ctf_disable", "1"))
			nvram_set("ctf_disable", "1");
		/* Foxconn added end pling 06/26/2014 */

    /* Foxconn added start pling 08/19/2010 */
    /* Make sure the NVRAM "ctf_disable" exist, otherwise 
     * MultiSsidCntrl will not work.
     */
    if (nvram_get("ctf_disable") == NULL)
        nvram_set("ctf_disable", "1");
    /* Foxconn added end pling 08/19/2010 */
		if (!nvram_match("ctf_disable", "1"))
			eval("insmod", "ctf");
#if defined(__CONFIG_WAPI__) || defined(__CONFIG_WAPI_IAS__)
		wapi_mtd_restore();
#endif /* __CONFIG_WAPI__ || __CONFIG_WAPI_IAS__ */

     	


/* #ifdef BCMVISTAROUTER */
#ifdef __CONFIG_IPV6__
		eval("insmod", "ipv6");
#endif /* __CONFIG_IPV6__ */
/* #endif */

#ifdef __CONFIG_EMF__
		/* Load the EMF & IGMP Snooper modules */
		load_emf();
#endif /*  __CONFIG_EMF__ */
#if 0
#if defined(__CONFIG_HSPOT__) || defined(__CONFIG_NPS__)
		eval("insmod", "proxyarp");
#endif /*  __CONFIG_HSPOT__ || __CONFIG_NPS__ */
#endif
    /* Bob added start to avoid sending unexpected dad, 09/16/2009 */
#ifdef INCLUDE_IPV6
		if (nvram_match("ipv6ready","1"))
		{
			system("echo 0 > /proc/sys/net/ipv6/conf/default/dad_transmits");
		}else{
		/* Foxconn added start pling 12/06/2010 */
		/* By default ipv6_spi is inserted to system to drop all packets. */
		/*Foxconn modify start by Hank for change ipv6_spi path in rootfs 08/27/2012*/
    
		if (nvram_match("enable_ap_mode","1"))
			system("/sbin/insmod /lib/modules/2.6.36.4brcmarm+/kernel/lib/ipv6_spi.ko working_mode=\"ap\"");
		else
			system("/sbin/insmod /lib/modules/2.6.36.4brcmarm+/kernel/lib/ipv6_spi.ko");
		/*Foxconn modify end by Hank for change ipv6_spi path in rootfs 08/27/2012*/
		/* Foxconn added end pling 12/06/2010 */
		}
#endif
    /* Bob added end to avoid sending unexpected dad, 09/16/2009 */
        
        
		/* Foxconn added start pling 09/02/2010 */
		/* Need to initialise switch related NVRAM before 
		 * insert ethernet module.
		 
		/* Load kernel modules. Make sure dpsta is loaded before wl
		 * due to symbol dependency.
		 */
#ifdef __CONFIG_IGMP_SNOOPING__
		config_switch();
			if (nvram_match("enable_vlan", "enable")) 
				config_iptv_params();
#endif
		/* Foxconn added end pling 09/02/2010 */

        /* foxconn added start by Bob 12/12/2013, BRCM suggest not to enable rxchain power save */
        nvram_set("wl_rxchain_pwrsave_enable", "0");
        nvram_unset("wl0_rxchain_pwrsave_enable");
        nvram_unset("wl1_rxchain_pwrsave_enable");
        /* foxconn added end by Bob 12/12/2013, BRCM suggest not to enable rxchain power save */
        
        /* foxconn added start by Bob 03/10/2014, BRCM's workaround for bridge mode connect fail issue. */
        /* Reduce transmit power at 5G band, HT20,  OFDM MCS 0,1,2 Reduce from 21.5db to 20 db. */
        /* Foxconn modified start pling 05/30/2014 */
        /* BRCM ARES: this workaround is not needed for 6.37.15.13 driver */
        //nvram_set("pci/2/1/mcsbw205ghpo", "0xBA768888"); 
        if (!nvram_match("pci/2/1/mcsbw205ghpo", "0xBA768600"))
            nvram_set("pci/2/1/mcsbw205ghpo", "0xBA768600");
        /* foxconn added end by Bob 03/10/2014, BRCM's workaround for bridge mode connect fail issue. */
        
        
                
		//modules = nvram_get("kernel_mods") ? : "et bcm57xx wl";
		/*Foxconn modify start by Hank for insert dpsta 08/27/2012*/
		/*Foxconn modify start by Hank for insert proxyarp 10/05/2012*/
		modules = nvram_get("kernel_mods") ? : "proxyarp et dpsta wl"; /* foxconn wklin modified, 10/22/2008 */
		/*Foxconn modify end by Hank for insert proxyarp 10/05/2012*/
		/*Foxconn modify end by Hank for insert dpsta 08/27/2012*/

		foreach(module, modules, next){
            /*Foxconn, [MJ] for GPIO debugging. */
#ifdef WIFI_DISABLE
            if(strcmp(module, "wl")){
			    eval("insmod", module);
            }else
                cprintf("we don't insert wl.ko.\n");
#else
            eval("insmod", module);
#endif
        }
#ifdef __CONFIG_USBAP__
		/* We have to load USB modules after loading PCI wl driver so
		 * USB driver can decide its instance number based on PCI wl
		 * instance numbers (in hotplug_usb())
		 */
		eval("insmod", "usbcore");

        /* Foxconn, [MJ] start, we can't insert usb-storage easiler than
         * automount being started. */
#if 0

		eval("insmod", "usb-storage");
        /* Foxconn, [MJ], for debugging. */
        cprintf("--> insmod usb-storage.\n");
#endif
        /* Foxconn, [MJ] end, we can't insert usb-storage easiler than
         * automount being started. */
		{
			char	insmod_arg[128];
			int	i = 0, maxwl_eth = 0, maxunit = -1;
			char	ifname[16] = {0};
			int	unit = -1;
			char arg1[20] = {0};
			char arg2[20] = {0};
			char arg3[20] = {0};
			char arg4[20] = {0};
			char arg5[20] = {0};
			char arg6[20] = {0};
			char arg7[20] = {0};
			const int wl_wait = 3;	/* max wait time for wl_high to up */

			/* Save QTD cache params in nvram */
			sprintf(arg1, "log2_irq_thresh=%d", atoi(nvram_safe_get("ehciirqt")));
			sprintf(arg2, "qtdc_pid=%d", atoi(nvram_safe_get("qtdc_pid")));
			sprintf(arg3, "qtdc_vid=%d", atoi(nvram_safe_get("qtdc_vid")));
			sprintf(arg4, "qtdc0_ep=%d", atoi(nvram_safe_get("qtdc0_ep")));
			sprintf(arg5, "qtdc0_sz=%d", atoi(nvram_safe_get("qtdc0_sz")));
			sprintf(arg6, "qtdc1_ep=%d", atoi(nvram_safe_get("qtdc1_ep")));
			sprintf(arg7, "qtdc1_sz=%d", atoi(nvram_safe_get("qtdc1_sz")));

			eval("insmod", "ehci-hcd", arg1, arg2, arg3, arg4, arg5,
				arg6, arg7);

			/* Search for existing PCI wl devices and the max unit number used.
			 * Note that PCI driver has to be loaded before USB hotplug event.
			 * This is enforced in rc.c
			 */
			for (i = 1; i <= DEV_NUMIFS; i++) {
				sprintf(ifname, "eth%d", i);
				if (!wl_probe(ifname)) {
					if (!wl_ioctl(ifname, WLC_GET_INSTANCE, &unit,
						sizeof(unit))) {
						maxwl_eth = i;
						maxunit = (unit > maxunit) ? unit : maxunit;
					}
				}
			}

			/* Set instance base (starting unit number) for USB device */
			sprintf(insmod_arg, "instance_base=%d", maxunit + 1);
            /*Foxconn, [MJ] for GPIO debugging. */
#ifndef WIFI_DISABLE
			eval("insmod", "wl_high", insmod_arg);
#endif
			/* Hold until the USB/HSIC interface is up (up to wl_wait sec) */
			sprintf(ifname, "eth%d", maxwl_eth + 1);
			i = wl_wait;
			while (wl_probe(ifname) && i--) {
				sleep(1);
			}
			if (!wl_ioctl(ifname, WLC_GET_INSTANCE, &unit, sizeof(unit)))
				cprintf("wl%d is up in %d sec\n", unit, wl_wait - i);
			else
				cprintf("wl%d not up in %d sec\n", unit, wl_wait);
		}
#ifdef LINUX26
		mount("usbdeffs", "/proc/bus/usb", "usbfs", MS_MGC_VAL, NULL);
#else
		mount("none", "/proc/bus/usb", "usbdevfs", MS_MGC_VAL, NULL);
#endif /* LINUX26 */
#endif /* __CONFIG_USBAP__ */

#ifdef __CONFIG_WCN__
		modules = "scsi_mod sd_mod usbcore usb-ohci usb-storage fat vfat msdos";
		foreach(module, modules, next){
            /* Foxconn, [MJ] for debugging. */
            cprintf("--> insmod %s\n", ,module);
			eval("insmod", module);
		}	
#endif

#ifdef __CONFIG_SOUND__
		modules = "soundcore snd snd-timer snd-page-alloc snd-pcm snd-pcm-oss "
		        "snd-soc-core i2c-core i2c-algo-bit i2c-gpio snd-soc-bcm947xx-i2s "
		        "snd-soc-bcm947xx-pcm snd-soc-wm8750 snd-soc-wm8955 snd-soc-bcm947xx";
		foreach(module, modules, next)
			eval("insmod", module);
		mknod("/dev/dsp", S_IRWXU|S_IFCHR, makedev(14, 3));
		mkdir("/dev/snd", 0777);
		mknod("/dev/snd/controlC0", S_IRWXU|S_IFCHR, makedev(116, 0));
		mknod("/dev/snd/pcmC0D0c", S_IRWXU|S_IFCHR, makedev(116, 24));
		mknod("/dev/snd/pcmC0D0p", S_IRWXU|S_IFCHR, makedev(116, 16));
		mknod("/dev/snd/timer", S_IRWXU|S_IFCHR, makedev(116, 33));
#endif
	}
	/*Foxconn add start by Hank for enable WAN LED amber 12/07/2012*/
	/*Foxconn add start by Hank for disable WAN LED blinking 12/07/2012*/
#if defined(R7000)
	system("/usr/sbin/et robowr 0x0 0x10 0x3000");
	system("/usr/sbin/et robowr 0x0 0x12 0x78");    
	system("/usr/sbin/et robowr 0x0 0x14 0x01");    /* force port 0 to use LED function 1 */
#else
	system("/usr/sbin/et robowr 0x0 0x10 0x0022");
#endif
	/*Foxconn add end by Hank for disable WAN LED blinking 11/08/2012*/
	/*Foxconn add end by Hank for enable WAN LED amber 12/07/2012*/

	if (memcmp(lx_rel, "2.6.36", 6) == 0) {
		int fd;
		if ((fd = open("/proc/irq/163/smp_affinity", O_RDWR)) >= 0) {
			close(fd);
			if (!nvram_match("txworkq", "1")) {
				system("echo 2 > /proc/irq/163/smp_affinity");
				system("echo 2 > /proc/irq/169/smp_affinity");
			}
			system("echo 2 > /proc/irq/112/smp_affinity");
		}
	}
	
	system("echo 20480 > /proc/sys/vm/min_free_kbytes");    /*Bob added on 09/05/2013, Set min free memory to 20Mbytes in case allocate memory failed */
	
	/* Set a sane date */
	stime(&tm);

	dprintf("done\n");
}

/* States */
enum {
	RESTART,
	STOP,
	START,
	TIMER,
	IDLE,
	WSC_RESTART,
	WLANRESTART, /* Foxconn added by EricHuang, 11/24/2006 */
	PPPSTART    /* Foxconn added by EricHuang, 01/09/2008 */
};
static int state = START;
static int signalled = -1;

/* foxconn added start, zacker, 05/20/2010, @spec_1.9 */
static int next_state = IDLE;

static int
next_signal(void)
{
	int tmp_sig = next_state;
	next_state = IDLE;
	return tmp_sig;
}
/* foxconn added end, zacker, 05/20/2010, @spec_1.9 */

/* Signal handling */
static void
rc_signal(int sig)
{
	if (state == IDLE) {	
		if (sig == SIGHUP) {
			dprintf("signalling RESTART\n");
			signalled = RESTART;
		}
		else if (sig == SIGUSR2) {
			dprintf("signalling START\n");
			signalled = START;
		}
		else if (sig == SIGINT) {
			dprintf("signalling STOP\n");
			signalled = STOP;
		}
		else if (sig == SIGALRM) {
			dprintf("signalling TIMER\n");
			signalled = TIMER;
		}
		else if (sig == SIGUSR1) {
			dprintf("signalling WSC RESTART\n");
			signalled = WSC_RESTART;
		}
		/* Foxconn modified start by EricHuang, 01/09/2008 */
		else if (sig == SIGQUIT) {
		    dprintf("signalling WLANRESTART\n");
		    signalled = WLANRESTART;
		}
		else if (sig == SIGILL) {
		    signalled = PPPSTART;
		}
		/* Foxconn modified end by EricHuang, 01/09/2008 */
	}
	/* foxconn added start, zacker, 05/20/2010, @spec_1.9 */
	else if (next_state == IDLE)
	{
		if (sig == SIGHUP) {
			dprintf("signalling RESTART\n");
			next_state = RESTART;
		}
		else if (sig == SIGUSR2) {
			dprintf("signalling START\n");
			next_state = START;
		}
		else if (sig == SIGINT) {
			dprintf("signalling STOP\n");
			next_state = STOP;
		}
		else if (sig == SIGALRM) {
			dprintf("signalling TIMER\n");
			next_state = TIMER;
		}
		else if (sig == SIGUSR1) {
			dprintf("signalling WSC RESTART\n");
			next_state = WSC_RESTART;
		}
		else if (sig == SIGQUIT) {
			printf("signalling WLANRESTART\n");
			next_state = WLANRESTART;
		}
		else if (sig == SIGILL) {
			next_state = PPPSTART;
		}
	}
	/* foxconn added end, zacker, 05/20/2010, @spec_1.9 */
}

/* Get the timezone from NVRAM and set the timezone in the kernel
 * and export the TZ variable
 */
static void
set_timezone(void)
{
	time_t now;
	struct tm gm, local;
	struct timezone tz;
	struct timeval *tvp = NULL;

	/* Export TZ variable for the time libraries to
	 * use.
	 */
	setenv("TZ", nvram_get("time_zone"), 1);

	/* Update kernel timezone */
	time(&now);
	gmtime_r(&now, &gm);
	localtime_r(&now, &local);
	tz.tz_minuteswest = (mktime(&gm) - mktime(&local)) / 60;
	settimeofday(tvp, &tz);

#if defined(__CONFIG_WAPI__) || defined(__CONFIG_WAPI_IAS__)
#ifndef	RC_BUILDTIME
#define	RC_BUILDTIME	1252636574
#endif
	{
		struct timeval tv = {RC_BUILDTIME, 0};

		time(&now);
		if (now < RC_BUILDTIME)
			settimeofday(&tv, &tz);
	}
#endif /* __CONFIG_WAPI__ || __CONFIG_WAPI_IAS__ */
}

/* Timer procedure.Gets time from the NTP servers once every timer interval
 * Interval specified by the NVRAM variable timer_interval
 */
int
do_timer(void)
{
	int interval = atoi(nvram_safe_get("timer_interval"));

	dprintf("%d\n", interval);

	if (interval == 0)
		return 0;

	/* Report stats */
	if (nvram_invmatch("stats_server", "")) {
		char *stats_argv[] = { "stats", nvram_get("stats_server"), NULL };
		_eval(stats_argv, NULL, 5, NULL);
	}

	/* Sync time */
	start_ntpc();

	alarm(interval);

	return 0;
}

/* Foxconn add start, Edward zhang, 09/14/2012, @add ARP PROTECTION support for RU SKU*/
#ifdef ARP_PROTECTION
static int getTokens(char *str, char *delimiter, char token[][C_MAX_TOKEN_SIZE], int maxNumToken)
{
    char temp[16*1024];    
    char *field;
    int numToken=0, i, j;
    char *ppLast = NULL;

    /* Check for empty string */
    if (str == NULL || str[0] == '\0')
        return 0;
   
    /* Now get the tokens */
    strcpy(temp, str);
    
    for (i=0; i<maxNumToken; i++)
    {
        if (i == 0)
            field = strtok_r(temp, delimiter, &ppLast);
        else 
            field = strtok_r(NULL, delimiter, &ppLast);

        /* Foxconn modified start, Wins, 06/27/2010 */
        //if (field == NULL || field[0] == '\0')
        if (field == NULL || (field != NULL && field[0] == '\0'))
        /* Foxconn modified end, Wins, 06/27/2010 */
        {
            for (j=i; j<maxNumToken; j++)
                token[j][0] = '\0';
            break;
        }

        numToken++;
        strcpy(token[i], field);
    }

    return numToken;
}

static int getReservedAddr(char reservedMacAddr[][C_MAX_TOKEN_SIZE], char reservedIpAddr[][C_MAX_TOKEN_SIZE])
/* Foxconn modified end, zacker, 10/31/2008, @lan_setup_change */
{
    int numReservedMac=0, numReservedIp=0;
    char *var;
    
    /* Read MAC and IP address tokens */
    if ( (var = acosNvramConfig_get("dhcp_resrv_mac")) != NULL )
    {
        numReservedMac = getTokens(var, " ", reservedMacAddr, C_MAX_RESERVED_IP);
    }
    
    if ( (var=acosNvramConfig_get("dhcp_resrv_ip")) != NULL )
    {
        numReservedIp = getTokens(var, " ", reservedIpAddr, C_MAX_RESERVED_IP);
    }
    
    if (numReservedMac != numReservedIp)
    {
        printf("getReservedAddr: reserved mac and ip not match\n");
    }
    
    return (numReservedMac<numReservedIp ? numReservedMac:numReservedIp);
}

static void config_arp_table(void)
{
    if(acosNvramConfig_match("arp_enable","enable"))
    {
        int i;
        char resrvMacAddr[C_MAX_RESERVED_IP][C_MAX_TOKEN_SIZE];
        char resrvIpAddr[C_MAX_RESERVED_IP][C_MAX_TOKEN_SIZE];
        int numResrvAddr = getReservedAddr(resrvMacAddr, resrvIpAddr);
        char arp_cmd[64];
        for (i=0; i<numResrvAddr; i++)
        {
            sprintf(arp_cmd,"arp -s %s %s",resrvIpAddr[i],resrvMacAddr[i]);
            printf("%s\n",arp_cmd);
            system(arp_cmd);
        }
    }
    
    return 0;
}
#endif
/* Foxconn add end, Edward zhang, 09/14/2012, @add ARP PROTECTION support for RU SKU*/
/* Main loop */
static void
main_loop(void)
{
#ifdef CAPI_AP
	static bool start_aput = TRUE;
#endif
	sigset_t sigset;
	pid_t shell_pid = 0;
#ifdef __CONFIG_VLAN__
	uint boardflags;
#endif

    /* foxconn wklin added start, 10/22/2008 */
	sysinit();

	/* Foxconn added start pling 03/20/2014 */
	/* Router Spec Rev 12: disable/enable ethernet interface when dhcp server start */
	eval("landown");
	/* Foxconn added end pling 03/20/2014 */

	/* Add loopback */
	config_loopback();
	/* Restore defaults if necessary */
	//restore_defaults(); /* foxconn removed, zacker, 08/06/2010, move to sysinit() */

	/* Convert deprecated variables */
	convert_deprecated();

	/* Upgrade NVRAM variables to MBSS mode */
	upgrade_defaults();

    /* Read ethernet MAC, etc */
    //eval("read_bd"); /* foxconn removed, zacker, 08/06/2010, move to sysinit() */
    /* foxconn wklin added end, 10/22/2008 */

    /* Reset some wps-related parameters */
    nvram_set("wps_start",   "none");
    /* foxconn added start, zacker, 05/20/2010, @spec_1.9 */
    nvram_set("wps_status", "0"); /* start_wps() */
    nvram_set("wps_proc_status", "0");
    /* foxconn added end, zacker, 05/20/2010, @spec_1.9 */
    
    nvram_set("enable_smart_mesh", "0");
    /* Foxconn Perry added start, 2011/05/13, for IPv6 router advertisment prefix information */
    /* reset IPv6 obsolete prefix information after reboot */
    nvram_set("radvd_lan_obsolete_ipaddr", "");
    nvram_set("radvd_lan_obsolete_ipaddr_length", "");
    nvram_set("radvd_lan_new_ipaddr", "");
    nvram_set("radvd_lan_new_ipaddr_length", "");
    /* Foxconn Perry added end, 2011/05/13, for IPv6 router advertisment prefix information */
    
#if defined(R7000)
    if (nvram_match("internal_antenna", "1"))
    {
        system("gpio 10 1");
        system("gpio 19 0");
    }
    else
    {
        system("gpio 10 0");
        system("gpio 19 1");
    }
#endif

    /* Foxconn added start, zacker, 06/17/2010, @new_tmp_lock */
    /* do this in case "wps_aplockdown_forceon" is set to "1" for tmp_lock
     * purpose but then there are "nvram_commit" and "reboot" action
     */
    if (nvram_match("wsc_pin_disable", "1"))
        nvram_set("wps_aplockdown_forceon", "1");
    else
        nvram_set("wps_aplockdown_forceon", "0");
    /* Foxconn added end, zacker, 06/17/2010, @new_tmp_lock */

    /* Foxconn added start, James Hsu, 06/17/2015 @ for extender mode can connect to root AP by wps */
    if(acosNvramConfig_match("enable_extender_mode","1"))
        acosNvramConfig_set("wps_pbc_apsta","enabled");
    else
        acosNvramConfig_set("wps_pbc_apsta","disabled");
    /* Foxconn added end, James Hsu, 06/17/2015 @ for extender mode can connect to root AP by wps */

    /* Foxconn added start, Wins, 04/20/2011, @RU_IPTV */
#ifdef CONFIG_RUSSIA_IPTV
/* Foxconn modified, Edward zhang, 09/05/2012, @add IPTV support for PR SKU*/
#if 0
    if ((!is_russia_specific_support()) && (!is_china_specific_support()))
    {
        nvram_set(NVRAM_IPTV_ENABLED, "0");
        nvram_set(NVRAM_IPTV_INTF, "0x00");
    }
#endif
#endif /* CONFIG_RUSSIA_IPTV */
    /* Foxconn added end, Wins, 04/20/2011, @RU_IPTV */
/* Foxconn add start, Edward zhang, 09/14/2012, @add ARP PROTECTION support for RU SKU*/
    if ((!is_russia_specific_support()) && (!is_china_specific_support()))
    {
        nvram_set(NVRAM_ARP_ENABLED, "disable");
    }
/* Foxconn add end, Edward zhang, 09/14/2012, @add ARP PROTECTION support for RU SKU*/
    /* Foxconn add start, Max Ding, 02/26/2010 */
#ifdef RESTART_ALL_PROCESSES
    nvram_unset("restart_all_processes");
#endif
    /* Foxconn add end, Max Ding, 02/26/2010 */


	/* Basic initialization */
	//sysinit();

	/* Setup signal handlers */
	signal_init();
	signal(SIGHUP, rc_signal);
	signal(SIGUSR2, rc_signal);
	signal(SIGINT, rc_signal);
	signal(SIGALRM, rc_signal);
	signal(SIGUSR1, rc_signal);	
	signal(SIGQUIT, rc_signal); /* Foxconn added by EricHuang, 11/24/2006 */
	signal(SIGILL, rc_signal); //ppp restart
	sigemptyset(&sigset);

	/* Give user a chance to run a shell before bringing up the rest of the system */
	if (!noconsole)
		run_shell(1, 0);

	/* Get boardflags to see if VLAN is supported */
#ifdef __CONFIG_VLAN__
	boardflags = strtoul(nvram_safe_get("boardflags"), NULL, 0);
#endif	/* __CONFIG_VLAN__ */


#if 0 /* foxconn modified, wklin 10/22/2008, move the the start of this function */
	/* Add loopback */
	config_loopback();

	/* Convert deprecated variables */
	convert_deprecated();



	/* Upgrade NVRAM variables to MBSS mode */
	upgrade_defaults();

	/* Restore defaults if necessary */
	restore_defaults();


    /* Foxconn added start pling 06/20/2007 */
    /* Read board data again, since the "restore_defaults" action
     * above will overwrite some of our settings */
    eval("read_bd");
    /* Foxconn added end pling 06/20/2006 */
#endif /* 0 */
    
#ifdef __CONFIG_NAT__
	/* Auto Bridge if neccessary */
	if (!strcmp(nvram_safe_get("auto_bridge"), "1"))
	{
		auto_bridge();
	}
	/* Setup wan0 variables if necessary */
	set_wan0_vars();
#endif	/* __CONFIG_NAT__ */

    /* Foxconn added start pling 07/13/2009 */
    /* create the USB semaphores */
#ifdef SAMBA_ENABLE
    usb_sem_init(); //[MJ] for 5G crash
#endif
    /* Foxconn added end pling 07/13/2009 */

#if defined(__CONFIG_FAILSAFE_UPGRADE_SUPPORT__)
	nvram_set(PARTIALBOOTS, "0");
	nvram_commit();
#endif

	/* Loop forever */
	for (;;) {
		switch (state) {
		case RESTART:
			dprintf("RESTART\n");
			/* Fall through */
			/* Foxconn added start pling 06/14/2007 */
            /* When vista finished configuring this router (wl0_wps_config_state: 0->1),
             * then we come here to restart WLAN 
             */
            stop_wps();
			stop_nas();
            stop_eapd();
			stop_bcmupnp();
			stop_wlan();
				stop_bsd();
			/*Foxconn add start by Hank 06/14/2012*/
			/*Enable 2.4G auto channel detect, kill acsd for stop change channel*/
			if((nvram_match("wla_channel", "0") || nvram_match("wlg_channel", "0")) && nvram_match("enable_sta_mode","0"))
				stop_acsd();
			/*Foxconn add end by Hank 06/14/2012*/

    	    convert_wlan_params();  /* For WCN , added by EricHuang, 12/21/2006 */
            sleep(2);               /* Wait some time for wsc, etc to terminate */

            /* if "unconfig" to "config" mode, force it to built-in registrar and proxy mode */
            /* added start by EricHuang, 11/04/2008 */
            if ( nvram_match("wps_status", "0") ) //restart wlan for wsc
            {
                nvram_set("lan_wps_reg", "enabled");
                nvram_set("wl_wps_reg", "enabled");
                nvram_set("wl0_wps_reg", "enabled");
#if (defined INCLUDE_DUAL_BAND)
                nvram_set("wl0_wps_reg", "enabled");
#endif
                /* Foxconn modify start, Max Ding, 08/28/2010 for NEW_BCM_WPS */
                /* New NVRAM to BSP 5.22.83.0, 'wlx_wps_config_state' not used anymore. */
                //printf("restart -- wl0_wps_config_state=%s\n", nvram_get("wl0_wps_config_state"));
                //nvram_set("wl_wps_config_state", nvram_get("wl0_wps_config_state"));
                if ( nvram_match("lan_wps_oob", "enabled") )
                {
                    nvram_set("wl_wps_config_state", "0");
                    nvram_set("wl0_wps_config_state", "0");
#if (defined INCLUDE_DUAL_BAND)
                    nvram_set("wl1_wps_config_state", "0");
#endif
                }
                else
                {
                    nvram_set("wl_wps_config_state", "1");
                    nvram_set("wl0_wps_config_state", "1");
#if (defined INCLUDE_DUAL_BAND)
                    nvram_set("wl1_wps_config_state", "1");
#endif
                }
                /* Foxconn modify end, Max Ding, 08/28/2010 */
            }
            /* added end by EricHuang, 11/04/2008 */
            
            /* hide unnecessary warnings (Invaid XXX, out of range xxx etc...)*/
            {
                #include <fcntl.h>
                int fd1, fd2;
                fd1 = dup(2);
                fd2 = open("/dev/null", O_WRONLY);
                close(2);
                dup2(fd2, 2);
                close(fd2);
#if ((defined WLAN_REPEATER) || (defined CONFIG_EXTENDER_MODE)) && (defined INCLUDE_DUAL_BAND)
                if(acosNvramConfig_match("enable_extender_mode", "1"))
                    add_wl_if_for_br0();/* Foxconn added by Max Ding, 11/10/2011 @wps auto change mode  */
#endif
                start_wlan(); //<-- to hide messages generated here
                close(2);
                dup2(fd1, 2);
                close(fd1);
            }
            
            save_wlan_time();          
            start_bcmupnp();
            start_eapd();           /* Foxconn modify by aspen Bai, 10/08/2008 */
            start_nas();            /* Foxconn modify by aspen Bai, 08/01/2008 */
            start_wps();            /* Foxconn modify by aspen Bai, 08/01/2008 */
            sleep(2);               /* Wait for WSC to start */
            /* Foxconn add start by aspen Bai, 09/10/2008 */
            /* Must call it when start wireless */
            start_wl();
            /* Foxconn add end by aspen Bai, 09/10/2008 */
			/*Foxconn add start by Antony 06/16/2013 Start the bandsteering*/

    
      if((strcmp(nvram_safe_get("wla_ssid"),nvram_safe_get("wlg_ssid") )!=0))
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),nvram_safe_get("wlg_secu_type") )!=0)
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),"None") || strcmp(nvram_safe_get("wlg_secu_type"),"None"))
      {
          if(strcmp(nvram_safe_get("wla_passphrase"),nvram_safe_get("wlg_passphrase"))!=0) 
              nvram_set("enable_band_steering", "0");
      }
			if(nvram_match("enable_band_steering", "1") && nvram_match("wla_wlanstate", "Enable")&& nvram_match("wlg_wlanstate", "Enable"))
				start_bsd();
			/*Foxconn add end by Antony 06/16/2013*/

			/*Foxconn add start by Hank 06/14/2012*/
			/*Enable 2.4G auto channel detect, call acsd to start change channel*/
			//if((nvram_match("wla_channel", "0") || nvram_match("wlg_channel", "0")) && nvram_match("enable_sta_mode","0"))
			if(nvram_match("enable_sta_mode","0"))
				start_acsd();
			/*Foxconn add end by Hank 06/14/2012*/
            nvram_commit();         /* Save WCN obtained parameters */

			/* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
			//state = IDLE;
			state = next_signal();
			/* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */

#if 0       /* foxconn removed start, zacker, 09/17/2009, @wps_led */
#ifdef BCM4716
			if (nvram_match("wla_secu_type", "None"))
			{
				system("/sbin/gpio 7 0");
			}
			else
			{
				system("/sbin/gpio 7 1");
			}
#else
			if (nvram_match("wla_secu_type", "None"))
			{
				system("/sbin/gpio 1 0");
			}
			else
			{
				system("/sbin/gpio 1 1");
			}
#endif
#endif      /* foxconn removed end, zacker, 09/17/2009, @wps_led */
			
			break;
			/* Foxconn added end pling 06/14/2007 */

		case STOP:
			dprintf("STOP\n");
			pmon_init();
			
      if(nvram_match ("wireless_restart", "1"))
      {
            stop_wps();
            stop_nas();
            stop_eapd(); 
    				stop_bsd();
      }
      
      stop_bcmupnp();
			
			stop_lan();
#ifdef __CONFIG_VLAN__
			if (boardflags & BFL_ENETVLAN)
				stop_vlan();
#endif	/* __CONFIG_VLAN__ */
			if (state == STOP) {
				/* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
				//state = IDLE;
				state = next_signal();
				/* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */
				break;
			}
			/* Fall through */
		case START:
			dprintf("START\n");

            /* Foxconn Perry added start, 01/30/2015, for extender mode */
#ifdef CONFIG_EXTENDER_MODE            
            /* If previous firmware configure to station mode or WDS mode */
            /* configure it to extender mode */
/* Foxconn Antony added start 07/22/2015, remove the extender mode */
#if 0
            if(nvram_match("enable_sta_mode", "1") ||
                nvram_match("wla_wds_enable", "1") ||
                nvram_match("wlg_wds_enable", "1"))
                config_extender_mode();
#endif                
/* Foxconn Antony added end 07/22/2015 */
#endif /* CONFIG_EXTENDER_MODE */
            /* Foxconn Perry added end, 01/30/2015, for extender mode */

			pmon_init();
			/* foxconn added start, zacker, 01/13/2012, @iptv_igmp */
#ifdef CONFIG_RUSSIA_IPTV
			if (!nvram_match("wla_repeater", "1")
#if (defined INCLUDE_DUAL_BAND)
				&& !nvram_match("wlg_repeater", "1")
#endif
				)
			{
				/* don't do this in cgi since "rc stop" need to do cleanup */
				config_iptv_params();
				/* always do this to active new vlan settings */
				active_vlan();
			}
#endif /* CONFIG_RUSSIA_IPTV */

#if (defined INCLUDE_QOS) || (defined __CONFIG_IGMP_SNOOPING__)
			config_switch_reg();
#endif
			/* foxconn added end, zacker, 01/13/2012, @iptv_igmp */
		/*foxconn added start, water, 12/21/09*/
#ifdef RESTART_ALL_PROCESSES
		if ( nvram_match("restart_all_processes", "1") )
		{
			restore_defaults();
			eval("read_bd");
			convert_deprecated();
			/* Foxconn add start, Max Ding, 03/03/2010 */

#if (defined BCM5325E) || (defined BCM53125)
			system("/usr/sbin/et robowr 0x34 0x00 0x00e0");
#endif
			/* Foxconn add end, Max Ding, 03/03/2010 */
#if !defined(U12H245) && !defined(U12H264) && !defined(U12H268)
			if(acosNvramConfig_match("emf_enable", "1") )
			{
    			system("insmod emf");
    			system("insmod igs");
    			system("insmod wl");
			}
#endif			
		}
#endif

		/*foxconn added end, water, 12/21/09*/
			{ /* Set log level on restart */
				char *loglevel;
				int loglev = 8;

				if ((loglevel = nvram_get("console_loglevel"))) {
					loglev = atoi(loglevel);
				}
				klogctl(8, NULL, loglev);
				if (loglev < 7) {
					printf("WARNING: console log level set to %d\n", loglev);
				}
			}

			set_timezone();
#ifdef __CONFIG_VLAN__
			if (boardflags & BFL_ENETVLAN)
				start_vlan();
#endif	/* __CONFIG_VLAN__ */
            /* wklin modified start, 10/23/2008 */
            /* hide unnecessary warnings (Invaid XXX, out of range xxx etc...)*/
            {
                #include <fcntl.h>
                int fd1, fd2;
                fd1 = dup(2);
                fd2 = open("/dev/null", O_WRONLY);
                close(2);
                dup2(fd2, 2);
                close(fd2);
                start_lan(); //<-- to hide messages generated here
      if(nvram_match ("wireless_restart", "1"))
                start_wlan(); //<-- need it to bring up 5G interface
                close(2);
                dup2(fd1, 2);
                close(fd1);
            }
            if (nvram_match("wla_repeater", "1")
#if (defined INCLUDE_DUAL_BAND)
            || nvram_match("wlg_repeater", "1")
#endif
            )
            {
                /* if repeater mode, del vlan1 from br0 and disable vlan */
#ifdef BCM4716
                system("/usr/sbin/brctl delif br0 vlan0");
                system("/usr/sbin/et robowr 0x34 0x00 0x00");
#else
                /*foxconn modified start, water, 01/07/10, @lan pc ping DUT failed when repeater mode & igmp enabled*/
                //system("/usr/sbin/brctl delif br0 vlan1");
                //system("/usr/sbin/et robowr 0x34 0x00 0x00");
#ifdef IGMP_PROXY
                if (!nvram_match("igmp_proxying_enable", "1"))
#endif
                {
                system("/usr/sbin/brctl delif br0 vlan1");
                system("/usr/sbin/et robowr 0x34 0x00 0x00");
                }
                /*foxconn modified end, water, 01/07/10*/
#endif
            }
            /* wklin modified end, 10/23/2008 */           

            /*Foxconn Tab Tseng add start, 2015/03/30, disable WPS & smartconnet when smart mesh enable */
#ifdef CONFIG_SMART_MESH
            if (!nvram_match("enable_smart_mesh", "0"))
            {
                acosNvramConfig_set("enable_smart_mesh", "1");
                acosNvramConfig_set("enable_band_steering", "0");
                acosNvramConfig_set("wps_aplockdown_disable", "1");
                acosNvramConfig_set("wsc_pin_disable", "1");
                acosNvramConfig_set("wl0_wps_config_state", "0");
                acosNvramConfig_set("wl1_wps_config_state", "0"); 

            }
#endif			
            /*Foxconn Tab Tseng add start, 2015/03/30, disable WPS & smartconnet when smart mesh enable */
            
            save_wlan_time();
			start_bcmupnp();
      if(nvram_match ("wireless_restart", "1"))
      {

            start_eapd();
            start_nas();
            start_wps();
            sleep(2);
            start_wl();
			/*Foxconn add start by Hank 06/14/2012*/
			/*Enable 2.4G auto channel detect, call acsd to start change channel*/

			//if((nvram_match("wla_channel", "0") || nvram_match("wlg_channel", "0")) && nvram_match("enable_sta_mode","0"))
			if(nvram_match("enable_sta_mode","0"))
				start_acsd();
			/*Foxconn add end by Hank 06/14/2012*/
    
      if((strcmp(nvram_safe_get("wla_ssid"),nvram_safe_get("wlg_ssid") )!=0))
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),nvram_safe_get("wlg_secu_type") )!=0)
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),"None") || strcmp(nvram_safe_get("wlg_secu_type"),"None"))
      {
          if(strcmp(nvram_safe_get("wla_passphrase"),nvram_safe_get("wlg_passphrase"))!=0) 
              nvram_set("enable_band_steering", "0");
      }


			if(nvram_match("enable_band_steering", "1") && nvram_match("wla_wlanstate", "Enable")&& nvram_match("wlg_wlanstate", "Enable"))
				start_bsd();
	  }
            /* Now start ACOS services */
            eval("acos_init");
            eval("acos_service", "start");

            /* Start wsc if it is in 'unconfiged' state, and if PIN is not disabled */
      if(nvram_match ("wireless_restart", "1"))
      {
            if (nvram_match("wl0_wps_config_state", "0") && !nvram_match("wsc_pin_disable", "1"))
            {
                /* if "unconfig" to "config" mode, force it to built-in registrar and proxy mode */
                nvram_set("wl_wps_reg", "enabled");
                nvram_set("wl0_wps_reg", "enabled");
                nvram_set("wps_proc_status", "0");
                nvram_set("wps_method", "1");
                //nvram_set("wps_config_command", "1");
            }

            /* Foxconn added start pling 03/30/2009 */
            /* Fix antenna diversiy per Netgear Bing's request */
#if 0//(!defined WNR3500v2VCNA)        // pling added 04/10/2009, vnca don't want fixed antenna
            eval("wl", "down");
            eval("wl", "nphy_antsel", "0x02", "0x02", "0x02", "0x02");
            eval("wl", "up");
#endif
            /* Foxconn added end pling 03/30/2009 */
            //eval("wl", "interference", "2");    // pling added 03/27/2009, per Netgear Fanny request

#if ( (defined SAMBA_ENABLE) || (defined HSDPA) )
                if (!acosNvramConfig_match("wla_wlanstate", "Enable") || acosNvramConfig_match("wifi_on_off", "0"))
                {/*water, 05/15/2009, @disable wireless, router will reboot continually*/
                 /*on WNR3500L, WNR3500U, MBR3500, it was just a work around..*/
                    eval("wl", "down");
                }
#endif
      if(nvram_match("wla_wds_enable","1"))
          nvram_set("wds_wifi_restart","1");
      else
          nvram_set("wds_wifi_restart","0");
			/* Fall through */
		  }
      nvram_set ("wireless_restart", "1");		  
		case TIMER:
            /* Foxconn removed start pling 07/12/2006 */
#if 0
			dprintf("TIMER\n");
			do_timer();
#endif
            /* Foxconn removed end pling 07/12/2006 */
			/* Fall through */
		case IDLE:
			dprintf("IDLE\n");
			/* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
			//state = IDLE;
			state = next_signal();
			if (state != IDLE)
				break;
			/* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */

#ifdef CAPI_AP
			if (start_aput == TRUE) {
				system("/usr/sbin/wfa_aput_all&");
				start_aput = FALSE;
			}
#endif /* CAPI_AP */

			/* foxconn added start, zacker, 09/17/2009, @wps_led */
			if (nvram_match("wps_start",   "none"))
			    /* Foxconn add modified, Tony W.Y. Wang, 12/03/2009 */
				//send_wps_led_cmd(WPS_LED_BLINK_OFF, 0);
				if (acosNvramConfig_match("dome_led_status", "ON"))
                    send_wps_led_cmd(WPS_LED_BLINK_OFF, 3);
                else if (acosNvramConfig_match("dome_led_status", "OFF"))
                    send_wps_led_cmd(WPS_LED_BLINK_OFF, 2);
			/* foxconn added end, zacker, 09/17/2009, @wps_led */
			
			    /* Foxconn added start by Jesse Chen, 11/16/2012 */    
#if defined(INCLUDE_QUICK_QOS) 
                if(acosNvramConfig_match("qos_enable", "1")
                    &&acosNvramConfig_match("quick_qos_mode", "1")
                    &&acosNvramConfig_match("wps_fasttrack_button_status", "1"))
                {                   
                    //wps button is set for fast lane                    
                    if(acosNvramConfig_match("qos_fasttrack_enable", "1"))
                        send_wps_led_cmd(WPS_LED_CHANGE_GREEN, 0);
                    else
                        send_wps_led_cmd(WPS_LED_CHANGE_GREEN, 1);            
                }
#endif	        
                /* Foxconn added start by Jesse Chen, 11/16/2012 */

			/* Wait for user input or state change */
			while (signalled == -1) {
				if (!noconsole && (!shell_pid || kill(shell_pid, 0) != 0))
					shell_pid = run_shell(0, 1);
				else {

					sigsuspend(&sigset);
				}
#ifdef LINUX26
				/*Foxconn modify start by Hank 07/31/2013*/
				/*for speed up USB3.0 throughput*/
				system("echo 1 > /proc/sys/vm/drop_caches");
				//system("echo 4096 > /proc/sys/vm/min_free_kbytes");
				/*Foxconn modify end by Hank 07/31/2013*/
#elif defined(__CONFIG_SHRINK_MEMORY__)
				eval("cat", "/proc/shrinkmem");
#endif	/* LINUX26 */
			}
			state = signalled;
			signalled = -1;
			break;

		case WSC_RESTART:
			dprintf("WSC_RESTART\n");
			/* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
			//state = IDLE;
			state = next_signal();
			/* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */
			stop_wps();    /* Foxconn modify by aspen Bai, 08/01/2008 */
			start_wps();    /* Foxconn modify by aspen Bai, 08/01/2008 */
			break;

            /* Foxconn added start pling 06/14/2007 */
            /* We come here only if user press "apply" in Wireless GUI */
		case WLANRESTART:
                    dprintf("WLANRESTART\n");
		
		    stop_wps(); 
		    stop_nas();
            stop_eapd();
            stop_bcmupnp();

			/*Foxconn add start by Antony 06/16/2013*/
				stop_bsd();
			/*Foxconn add end by Antony 06/16/2013*/
            
			stop_wlan();
            
			/*Foxconn add start by Hank 06/14/2012*/
			/*Enable 2.4G auto channel detect, kill acsd stop change channel*/
			if((nvram_match("wla_channel", "0") || nvram_match("wlg_channel", "0")) && nvram_match("enable_sta_mode","0"))
	            stop_acsd();
			/*Foxconn add end by Hank 06/14/2012*/
			eval("read_bd");    /* sync foxconn and brcm nvram params */
                       
            /* wklin modified start, 01/29/2007 */
            /* hide unnecessary warnings (Invaid XXX, out of range xxx etc...)*/
            {
                #include <fcntl.h>
                int fd1, fd2;
                fd1 = dup(2);
                fd2 = open("/dev/null", O_WRONLY);
                close(2);
                dup2(fd2, 2);
                close(fd2);
#if ((defined WLAN_REPEATER) || (defined CONFIG_EXTENDER_MODE)) && (defined INCLUDE_DUAL_BAND)
                if(acosNvramConfig_match("enable_extender_mode", "1"))
                    add_wl_if_for_br0();/* Foxconn added by Max Ding, 11/10/2011 @wps auto change mode  */
#endif
                start_wlan(); //<-- to hide messages generated here
                close(2);
                dup2(fd1, 2);
                close(fd1);
            }
            /* wklin modified end, 01/29/2007 */
            #if 0
            /* Foxconn add start, Tony W.Y. Wang, 03/25/2010 @Single Firmware Implementation */
            if (nvram_match("sku_name", "NA"))
            {
                printf("set wl country and power of NA\n");
                eval("wl", "country", "Q1/15");
                /* Foxconn modify start, Max Ding, 12/27/2010 "US/39->US/8" for open DFS band 2&3 channels */
                //eval("wl", "-i", "eth2", "country", "US/39");
                eval("wl", "-i", "eth2", "country", "Q1/15");
                /* Foxconn modify end, Max Ding, 12/27/2010 */
                /* Foxconn remove start, Max Ding, 12/27/2010 fix time zone bug for NA sku */
                //nvram_set("time_zone", "-8");
                /* Foxconn remove end, Max Ding, 12/27/2010 */
                nvram_set("wla_region", "11");
                nvram_set("wla_temp_region", "11");
                nvram_set("wl_country", "Q1");
                nvram_set("wl_country_code", "Q1");
                nvram_set("ver_type", "NA");
            }
            /*
            else if (nvram_match("sku_name", "WW"))
            {
                printf("set wl country and power of WW\n");
                eval("wl", "country", "EU/5");
                eval("wl", "-i", "eth2", "country", "EU/5");
                nvram_set("time_zone", "0");
                nvram_set("wla_region", "5");
                nvram_set("wla_temp_region", "5");
                nvram_set("wl_country", "EU5");
                nvram_set("wl_country_code", "EU5");
                nvram_set("ver_type", "WW");
            }
            */
            /* Foxconn add end, Tony W.Y. Wang, 03/25/2010 @Single Firmware Implementation */
            #endif
        if(!acosNvramConfig_match("restart_all_processes","1"))
        {
            
            save_wlan_time();
            start_bcmupnp();
            start_eapd();
            start_nas();
            start_wps();
            sleep(2);           /* Wait for WSC to start */
            start_wl();
#ifdef ARP_PROTECTION
            config_arp_table();
#endif 
			/*Foxconn add start by Hank 06/14/2012*/
			/*Enable 2.4G auto channel detect, call acsd to start change channel*/
			//if((nvram_match("wla_channel", "0") || nvram_match("wlg_channel", "0")) && nvram_match("enable_sta_mode","0"))
			if(nvram_match("enable_sta_mode","0"))
				start_acsd();
			/*Foxconn add end by Hank 06/14/2012*/

			/*Foxconn add start by Antony 06/16/2013 Start the bandsteering*/
    
      if((strcmp(nvram_safe_get("wla_ssid"),nvram_safe_get("wlg_ssid") )!=0))
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),nvram_safe_get("wlg_secu_type") )!=0)
          nvram_set("enable_band_steering", "0");      	

      if(strcmp(nvram_safe_get("wla_secu_type"),"None") || strcmp(nvram_safe_get("wlg_secu_type"),"None"))
      {
          if(strcmp(nvram_safe_get("wla_passphrase"),nvram_safe_get("wlg_passphrase"))!=0) 
              nvram_set("enable_band_steering", "0");
      }

			if(nvram_match("enable_band_steering", "1") && nvram_match("wla_wlanstate", "Enable")&& nvram_match("wlg_wlanstate", "Enable"))
				start_bsd();
			/*Foxconn add end by Antony 06/16/2013*/

            /* Start wsc if it is in 'unconfiged' state */
            if (nvram_match("wl0_wps_config_state", "0") && !nvram_match("wsc_pin_disable", "1"))
            {
                /* if "unconfig" to "config" mode, force it to built-in registrar and proxy mode */
                nvram_set("wl_wps_reg", "enabled");
                nvram_set("wl0_wps_reg", "enabled");
                nvram_set("wps_proc_status", "0");
                nvram_set("wps_method", "1");
                //nvram_set("wps_config_command", "1");
            }
			/* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
			//state = IDLE;
		 }
			state = next_signal();
			/* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */
		    break;
            /* Foxconn added end pling 06/14/2007 */
        /* Foxconn added start by EricHuang, 01/09/2008 */
		case PPPSTART:
		{
            //char *pptp_argv[] = { "pppd", NULL };
            char *pptp_argv[] = { "pppd", "file", "/tmp/ppp/options", NULL };

		    _eval(pptp_argv, NULL, 0, NULL);
		    
		    /* foxconn modified start, zacker, 05/20/2010, @spec_1.9 */
		    //state = IDLE;
		    state = next_signal();
		    /* foxconn modified end, zacker, 05/20/2010, @spec_1.9 */
		    break;
		}
		/* Foxconn added end by EricHuang, 01/09/2008 */
		    
		default:
			dprintf("UNKNOWN\n");
			return;
		}
	}
}

int
main(int argc, char **argv)
{
#ifdef LINUX26
	char *init_alias = "preinit";
#else
	char *init_alias = "init";
#endif
	char *base = strrchr(argv[0], '/');

	base = base ? base + 1 : argv[0];


	/* init */
#ifdef LINUX26
	if (strstr(base, "preinit")) {
		mount("devfs", "/dev", "tmpfs", MS_MGC_VAL, NULL);
		/* Michael added */
//        mknod("/dev/nvram", S_IRWXU|S_IFCHR, makedev(252, 0));
/*        mknod("/dev/mtdblock16", S_IRWXU|S_IFBLK, makedev(31, 16));
        mknod("/dev/mtdblock17", S_IRWXU|S_IFBLK, makedev(31, 17));
        mknod("/dev/mtd16", S_IRWXU|S_IFCHR, makedev(90, 32));
        mknod("/dev/mtd16ro", S_IRWXU|S_IFCHR, makedev(90, 33));
        mknod("/dev/mtd17", S_IRWXU|S_IFCHR, makedev(90, 34));
        mknod("/dev/mtd17ro", S_IRWXU|S_IFCHR, makedev(90, 35));*/
		/* Michael ended */
		mknod("/dev/console", S_IRWXU|S_IFCHR, makedev(5, 1));
		mknod("/dev/aglog", S_IRWXU|S_IFCHR, makedev(AGLOG_MAJOR_NUM, 0));
		mknod("/dev/wps_led", S_IRWXU|S_IFCHR, makedev(WPS_LED_MAJOR_NUM, 0));
#ifdef __CONFIG_UTELNETD__
		mkdir("/dev/pts", 0777);	
		mknod("/dev/pts/ptmx", S_IRWXU|S_IFCHR, makedev(5, 2));
		mknod("/dev/pts/0", S_IRWXU|S_IFCHR, makedev(136, 0));
		mknod("/dev/pts/1", S_IRWXU|S_IFCHR, makedev(136, 1));
#endif	/* __CONFIG_UTELNETD__ */
		/* Foxconn added start pling 12/26/2011, for WNDR4000AC */
#if (defined GPIO_EXT_CTRL)
		mknod("/dev/ext_led", S_IRWXU|S_IFCHR, makedev(EXT_LED_MAJOR_NUM, 0));
#endif
		/* Foxconn added end pling 12/26/2011 */
#else /* LINUX26 */
	if (strstr(base, "init")) {
#endif /* LINUX26 */
		main_loop();
		return 0;
	}

	/* Set TZ for all rc programs */
	setenv("TZ", nvram_safe_get("time_zone"), 1);

	/* rc [stop|start|restart ] */
	if (strstr(base, "rc")) {
		if (argv[1]) {
			if (strncmp(argv[1], "start", 5) == 0)
				return kill(1, SIGUSR2);
			else if (strncmp(argv[1], "stop", 4) == 0)
				return kill(1, SIGINT);
			else if (strncmp(argv[1], "restart", 7) == 0)
				return kill(1, SIGHUP);
		    /* Foxconn added start by EricHuang, 11/24/2006 */
		    else if (strcmp(argv[1], "wlanrestart") == 0)
		        return kill(1, SIGQUIT);
		    /* Foxconn added end by EricHuang, 11/24/2006 */
		} else {
			fprintf(stderr, "usage: rc [start|stop|restart|wlanrestart]\n");
			return EINVAL;
		}
	}

#ifdef __CONFIG_NAT__
	/* ppp */
	else if (strstr(base, "ip-up"))
		return ipup_main(argc, argv);
	else if (strstr(base, "ip-down"))
		return ipdown_main(argc, argv);

	/* udhcpc [ deconfig bound renew ] */
	else if (strstr(base, "udhcpc"))
		return udhcpc_wan(argc, argv);
#endif	/* __CONFIG_NAT__ */

#if 0 /* foxconn wklin removed, 05/14/2009 */
	/* ldhclnt [ deconfig bound renew ] */
	else if (strstr(base, "ldhclnt"))
		return udhcpc_lan(argc, argv);

	/* stats [ url ] */
	else if (strstr(base, "stats"))
		return http_stats(argv[1] ? : nvram_safe_get("stats_server"));
#endif

	/* erase [device] */
	else if (strstr(base, "erase")) {
		/* foxconn modified, zacker, 07/09/2010 */
		/*
		if (argv[1] && ((!strcmp(argv[1], "boot")) ||
			(!strcmp(argv[1], "linux")) ||
			(!strcmp(argv[1], "rootfs")) ||
			(!strcmp(argv[1], "nvram")))) {
		*/
		if (argv[1]) {
			return mtd_erase(argv[1]);
		} else {
			fprintf(stderr, "usage: erase [device]\n");
			return EINVAL;
		}
	}


	/* write [path] [device] */
	else if (strstr(base, "write")) {
		if (argc >= 3)
			return mtd_write(argv[1], argv[2]);
		else {
			fprintf(stderr, "usage: write [path] [device]\n");
			return EINVAL;
		}
	}

	/* hotplug [event] */
	else if (strstr(base, "hotplug")) {
		if (argc >= 2) {

			if (!strcmp(argv[1], "net"))
				return hotplug_net();
		/*foxconn modified start, water, @usb porting, 11/11/2008*/
/*#ifdef __CONFIG_WCN__
			else if (!strcmp(argv[1], "usb"))
				return hotplug_usb();
#endif*/
        /*for mount usb disks, 4m board does not need these codes.*/
#if (defined SAMBA_ENABLE || defined HSDPA) /* Foxconn add, FredPeng, 03/16/2009 @HSDPA */
			/* else if (!strcmp(argv[1], "usb"))
				return usb_hotplug(); */
				/*return hotplug_usb();*/
			else if (!strcmp(argv[1], "block"))
                return hotplug_block(); /* wklin modified, 02/09/2011 */
#endif
#if defined(LINUX_2_6_36)
			else if (!strcmp(argv[1], "platform"))
				return coma_uevent();
#endif /* LINUX_2_6_36 */

        /*foxconn modified end, water, @usb porting, 11/11/2008*/
		} else {
			fprintf(stderr, "usage: hotplug [event]\n");
			return EINVAL;
		}
	}

	return EINVAL;
}
