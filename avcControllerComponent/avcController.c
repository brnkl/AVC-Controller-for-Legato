/*******************************************************************************************************************
 
 AVC Controller

	This sample app aims to maintain a permanent connection with AirVantage.
	It automatically start or restart an AVC session whenever the connection is dropped due to:
		e.g. network loss, SIM/antenna removal/reinsert, anti-aging etc...

	Bu default, this controller automatically accept software download and installation requests.

	Usage:
		Use this controller to ensure a permanent connection with AirVantage.
		You can build and run your data applications (asset data, timeserie) without worrying about the connection.

	Note on AirVantage Queue Mode:
		- AirVantage-orginated commands are queued in the server, they will be sent to the device when it is is online.
		- You might find the situation where AirVantage-originated commands are not sent to device although
		  an AVC session is already active (per avcController action). This is due the NAT timeout (network tears down the NAT
		  if there is no data exchange within a network-specfic delay, could be as short as 20 seconds).
		  In this event, the external IP address of the device is no longer valid thus AirVantage cannot send queued commmands.
		- The NAT will be restored upon device reconnection (restart AVC session) or upon sending data (invoking dtls resume).
		- Using this avcController, if your application needs to receive server commands on a timely manner, then you'd need
		  to send data (le_avdata_Push) to AirVantage as often as you expect to receive AirVantage-originated commands.


	N. Chu
	June 2016

*******************************************************************************************************************/

#include "legato.h"
#include "interfaces.h"


#include <stdio.h>


#define APP_NAME                            "AVC_CONTROLLER"

#define AVCRETRYTIMERINTERVAL 				60	// AVC retry timer interval in seconds


const char* NetRegStateStr[] = 
{
	"LE_MRC_REG_NONE",
	"LE_MRC_REG_HOME",
	"LE_MRC_REG_SEARCHING",
	"LE_MRC_REG_DENIED",
	"LE_MRC_REG_ROAMING",
	"LE_MRC_REG_UNKNOWN"
};

le_timer_Ref_t 					 	AvcRetryTimerRef = NULL;
le_avc_StatusEventHandlerRef_t      AvcSessionHandle = NULL;  //reference to AirVantage Controller (AVC) Session handler

le_mrc_NetRegStateEventHandlerRef_t mrcNetRegStateHandle = NULL;




/* Forward Definitions */

le_result_t avcSessionOpen( void );
le_result_t avcSessionClose( void );



//-------------------------------------------------------------------------------------------------
/**
 * Timer handler for avc connection retry timer
 *	This is a one-shot timer, so be sure to delete it at the end
 */
//-------------------------------------------------------------------------------------------------

void AvcRetryTimerHandler( le_timer_Ref_t pTimerRef )
{
	LE_INFO("%s: avc Retry Timer fired!", APP_NAME);

	avcSessionOpen();		// try to reopen the avc Session

	LE_INFO("%s: avc Retry Timer finished", APP_NAME);
}

void StartAvcTimer()
{
	le_clk_Time_t   interval = { AVCRETRYTIMERINTERVAL, 0 };

	LE_INFO("%s:AVC Timer started: will retry in %d seconds...", APP_NAME, AVCRETRYTIMERINTERVAL);

	AvcRetryTimerRef = le_timer_Create("AvcRetryTimer");
	LE_WARN_IF( LE_OK != (le_timer_SetHandler( AvcRetryTimerRef, AvcRetryTimerHandler )), "SetHandler failed");
	LE_WARN_IF( LE_OK != (le_timer_SetRepeat( AvcRetryTimerRef, 1)), "SetRepeat failed");
	LE_WARN_IF( LE_OK != (le_timer_SetInterval(AvcRetryTimerRef, interval)), "SetInterval failed");
	LE_WARN_IF( LE_OK != (le_timer_Start( AvcRetryTimerRef )), "Start Failed");
}

void StopAvcTimer()
{
	if ( NULL != AvcRetryTimerRef )
	{
		LE_INFO("%s:AVC Timer stopped", APP_NAME);
		le_timer_Stop( AvcRetryTimerRef );
		le_timer_Delete( AvcRetryTimerRef );
		AvcRetryTimerRef = NULL;
	}
}



//-------------------------------------------------------------------------------------------------
/**
 * Fetch a string describing the type of update underway over Air Vantage.
 *
 * @return Pointer to a null-terminated string constant.
 */
//-------------------------------------------------------------------------------------------------
const char* GetUpdateType
(
	void
)
//--------------------------------------------------------------------------------------------------
{
	le_avc_UpdateType_t type;
	le_result_t res = le_avc_GetUpdateType(&type);
	if (res != LE_OK)
	{
		LE_CRIT("Unable to get update type (%s)", LE_RESULT_TXT(res));
		return "UNKNOWN";
	}
	else
	{
		switch (type)
		{
			case LE_AVC_FIRMWARE_UPDATE:
				return "FIRMWARE";

			case LE_AVC_APPLICATION_UPDATE:
				return "APPLICATION";

			case LE_AVC_FRAMEWORK_UPDATE:
				return "FRAMEWORK";

			case LE_AVC_UNKNOWN_UPDATE:
				return "UNKNOWN";
		}

		LE_CRIT("Unexpected update type %d", type);
		return "UNKNOWN";
	}
}

//-------------------------------------------------------------------------------------------------
/**
 * Status handler for avcService updates. Writes update status to configTree.
 */
//-------------------------------------------------------------------------------------------------
void avcSessionCtrl_StatusHandler
(
	le_avc_Status_t updateStatus,
	int32_t totalNumBytes,
	int32_t downloadProgress,
	void* contextPtr
)
//--------------------------------------------------------------------------------------------------
{
	const char* statusPtr = NULL;

	switch (updateStatus)
	{
		case LE_AVC_NO_UPDATE:
			statusPtr = "NO_UPDATE";
			break;
		case LE_AVC_DOWNLOAD_PENDING:
			statusPtr = "DOWNLOAD_PENDING";
			break;
		case LE_AVC_DOWNLOAD_IN_PROGRESS:
			statusPtr = "DOWNLOAD_IN_PROGRESS";
			break;
		case LE_AVC_DOWNLOAD_COMPLETE:
			statusPtr = "DOWNLOAD_COMPLETE";
			break;
		case LE_AVC_DOWNLOAD_FAILED:
			statusPtr = "DOWNLOAD_FAILED";
			break;
		case LE_AVC_INSTALL_PENDING:
			statusPtr = "INSTALL_PENDING";
			break;
		case LE_AVC_INSTALL_IN_PROGRESS:
			statusPtr = "INSTALL_IN_PROGRESS";
			break;
		case LE_AVC_INSTALL_COMPLETE:
			statusPtr = "INSTALL_COMPLETE";
			break;
		case LE_AVC_INSTALL_FAILED:
			statusPtr = "INSTALL_FAILED";
			break;
		case LE_AVC_UNINSTALL_PENDING:
			statusPtr = "UNINSTALL_PENDING";
			break;
		case LE_AVC_UNINSTALL_IN_PROGRESS:
			statusPtr = "UNINSTALL_IN_PROGRESS";
			break;
		case LE_AVC_UNINSTALL_COMPLETE:
			statusPtr = "UNINSTALL_COMPLETE";
			break;
		case LE_AVC_UNINSTALL_FAILED:
			statusPtr = "UNINSTALL_FAILED";
			break;

		case LE_AVC_CONNECTION_PENDING:
            statusPtr = "LE_AVC_CONNECTION_PENDING";
            break;
        case LE_AVC_AUTH_STARTED:
            statusPtr = "AUTHENTICATION_STARTED";
            break;
        case LE_AVC_AUTH_FAILED:
            statusPtr = "AUTHENTICATION_FAILED";
            break;
        case LE_AVC_REBOOT_PENDING:
            statusPtr = "REBOOT_PENDING";
            break;
        case LE_AVC_SESSION_BS_STARTED:
        	statusPtr = "SESSION_BS_STARTED";
        	break;
        case LE_AVC_CERTIFICATION_OK:
        	statusPtr = "CERTIFICATION_OK";
        	break;
        case LE_AVC_CERTIFICATION_KO:
        	statusPtr = "CERTIFICATION_KO";
        	break;
		case LE_AVC_SESSION_STARTED:
			statusPtr = "SESSION_STARTED";

			StopAvcTimer();		//stop trying stop avc

			break;

		case LE_AVC_SESSION_STOPPED:
			statusPtr = "SESSION_STOPPED";

			avcSessionOpen();

			break;

		case LE_AVC_SESSION_FAILED:
			statusPtr = "LE_AVC_SESSION_FAILED";
			break;
	}

	if (statusPtr == NULL)
	{
		LE_ERROR("%s: Air Vantage agent reported unexpected update status: %d", APP_NAME, updateStatus);
	}
	else
	{
		LE_INFO("%s: Air Vantage agent reported update status: %s", APP_NAME, statusPtr);

		le_result_t res;

		if (updateStatus == LE_AVC_DOWNLOAD_PENDING)
		{
			LE_INFO("%s: Accepting %s update.", APP_NAME, GetUpdateType());
			res = le_avc_AcceptDownload();
			if (res != LE_OK)
			{
				LE_ERROR("Failed to accept download from Air Vantage (%s)", LE_RESULT_TXT(res));
			}
		}
		else if (updateStatus == LE_AVC_INSTALL_PENDING)
		{
			LE_INFO("%s: Accepting %s installation.", APP_NAME, GetUpdateType());
			res = le_avc_AcceptInstall();
			if (res != LE_OK)
			{
				LE_ERROR("Failed to accept install from Air Vantage (%s)", LE_RESULT_TXT(res));
			}
		}
		else if (updateStatus == LE_AVC_UNINSTALL_PENDING)
		{
			LE_INFO("%s: Accepting %s uninstall.", APP_NAME, GetUpdateType());
			res = le_avc_AcceptUninstall();
			if (res != LE_OK)
			{
				LE_ERROR("Failed to accept uninstall from Air Vantage (%s)", LE_RESULT_TXT(res));
			}
		}
		else if (updateStatus == LE_AVC_INSTALL_COMPLETE)
		{
			//le_avc_StopSession();

			//avcSessionOpen();
		}
	}

}




/**
 * Open LWM2M session
 *	open/create a session
 */

le_result_t avcSessionOpen()
{
	le_result_t result = LE_FAULT;
	LE_INFO("%s: Retrying to Open AVC session...", APP_NAME);

	StopAvcTimer();

	sleep(2);

	le_mrc_NetRegState_t	netRegState;
	le_mrc_GetNetRegState(&netRegState);

	// Before starting an AVC session, check if the module is registered to network
	switch (netRegState)
	{
		case LE_MRC_REG_HOME:
		case LE_MRC_REG_ROAMING:
			LE_INFO("%s:      >Starting AVC session", APP_NAME);
			result = le_avc_StartSession();      // Start AVC session. Note: AVC handler must be registered prior starting a session
			if (LE_OK != result)
			{
				LE_INFO("%s:      >Start AVC session - Failed", APP_NAME);
			}
			else
			{
				LE_INFO("%s:      >Start AVC session - OK!", APP_NAME);
			}
			break;

		default:
			LE_INFO("%s:      >No Network", APP_NAME);
			break;
	}


	StartAvcTimer();

	return result;
}


/**
* Close & Stop LWM2M session
*/
le_result_t avcSessionClose()
{
	LE_INFO("%s: Closing AVC session", APP_NAME);

	le_avc_StopSession();

	if ( NULL != AvcRetryTimerRef )
	{
		le_timer_Stop( AvcRetryTimerRef );
		le_timer_Delete( AvcRetryTimerRef );
		AvcRetryTimerRef = NULL;
	}

	return LE_OK;
}



static void netRegStateHandle(le_mrc_NetRegState_t state, void *contextPtr)
{
	LE_INFO("%s: Network Registration state changed: [%d:%s]", APP_NAME, state, NetRegStateStr[state]);

	switch (state)
	{
		case LE_MRC_REG_HOME:
		case LE_MRC_REG_ROAMING:
			avcSessionOpen();        //start session when attached to network 
			break;

		case LE_MRC_REG_NONE:
		case LE_MRC_REG_SEARCHING:
		case LE_MRC_REG_DENIED:
		case LE_MRC_REG_UNKNOWN:
			le_avc_StopSession();    //on network loss, stop session so a new session could be restarted
			break;
	}
}



static void cleanUp()
{
	// Stop and Release all timers
	if ( NULL != AvcRetryTimerRef )
	{
		le_timer_Stop( AvcRetryTimerRef );
		le_timer_Delete( AvcRetryTimerRef );
		AvcRetryTimerRef = NULL;
	}

	if (NULL != AvcSessionHandle)
	{
		le_avc_RemoveStatusEventHandler( AvcSessionHandle );
		AvcSessionHandle = NULL;
	}

	if (NULL != mrcNetRegStateHandle)
	{
		le_mrc_RemoveNetRegStateEventHandler(mrcNetRegStateHandle);
		mrcNetRegStateHandle = NULL;
	}
}

static void sigHandlerSigTerm( int pSigNum )
{
	LE_INFO("%s: SIGTERM caught, release resource and quit app", APP_NAME);

	le_avc_StopSession();
	
	cleanUp();
}


COMPONENT_INIT
{
	LE_INFO("%s: Starting avcController", APP_NAME);

	// setup to catch application termination and shutdown cleanly
	le_sig_Block( SIGTERM );
	le_sig_SetEventHandler( SIGTERM, sigHandlerSigTerm );


	LE_INFO("%s: Registering AVC handler...", APP_NAME);
	AvcSessionHandle = le_avc_AddStatusEventHandler(avcSessionCtrl_StatusHandler, NULL);    //register a AVC handler
	LE_INFO("%s: AVC handler registered", APP_NAME);
	//le_avc_StopSession();


	mrcNetRegStateHandle = le_mrc_AddNetRegStateEventHandler(netRegStateHandle, NULL);

	//Open session at startup
    avcSessionOpen();
}
