#include <json/json.h>
#include <Windows.h>
#include <WinHttp.h>
#include <optional>
#include <string>
#include <format>
#include <iostream>

using namespace std::string_literals;

#undef ERROR

const char* SESSION_START_ERROR="Error starting session";
const char* CONNECTION_INITIALIZE_ERROR="Failed to initialize HTTP connection";
const char* CONNECTION_INITIATE_ERROR="Failed to initiate connection to server";
const char* CONNECTION_WEBSOCKET_ERROR="Failed to witch from HTTP connection to websocket";
const char* WEBSOCKET_RECEIVE_FAILED="Receive failed";
const char* WEBSOCKET_UPGRADE_FAILED="Failed to switch from HTTP to web socket";
const char* WEBSOCKET_CALLBACK_ATTACH_FAILED="Failed to attach status request callback";
const char* WEBSOCKET_PEEK_INVALID_PARAMETER="A parameter is invalid";
const char* WEBSOCKET_PEEK_INVALID_SERVER_RESPONSE="Invalid data was received from the server";
const char* WEBSOCKET_PEEK_CANCELLED="The operation was cancelled because socket is closing";

const std::string JSON_KEY_TYPE="type"s;
const std::string JSON_KEY_MESSAGE="message"s;

HINTERNET webSocketSession=nullptr;
HINTERNET webSocketConnection=nullptr;
HINTERNET webSocketRequest=nullptr;
HINTERNET webSocket=nullptr;

DWORD threadID;
HHOOK hook;
HWND window;

char buffer[1024]; // TODO: can this be dynamically allocated somehow? does the API call that uses it support that?

enum
{
	RM_START=WM_APP+1,
	RM_CONNECTION_INITIALIZE,
	RM_CONNECTION_INITIATE,
	RM_HTTP_RECEIVE_DATA,
	RM_RECEIVE_RESPONSE,
	RM_WEBSOCKET_INITIALIZE,
	RM_WEBSOCKET_DATA_AVAILABLE,
	RM_WEBSOCKET_DATA_RECEIVED,
	RM_WEBSOCKET_REQUEST_OPENED,
	RM_WEBSOCKET_CONNECTED,
	RM_WEBSOCKET_PEEK,
	RM_SYSTEM_ERROR
};

enum class ExitCode
{
	OK,
	ERROR,
	INVALID
};

struct NormalizedPoint
{
	double x;
	double y;
};

std::string targetTwitchChannelID;
std::string targetWindowTitle;

void Message(const std::string &type,const std::string &message)
{
	std::cout << "[" << type << "] " << message << std::endl;
}

void SystemMessage(const std::string &message)
{
	Message("System"s,message);
}

void ErrorMessage(const std::string &message)
{
	Message("Error"s,message);
}

void WindowsErrorMessage(MSG &message)
{
	LPSTR errorMessage=nullptr;
	size_t errorMessageSize=FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,message.lParam,MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),reinterpret_cast<LPSTR>(&errorMessage),0,NULL);
	//SystemMessage(std::format("{}: {}",reinterpret_cast<const char*>(message.wParam),std::string{errorMessage,errorMessageSize}));
	SystemMessage(std::format("{}: {}",reinterpret_cast<const char*>(message.wParam),std::string{errorMessage,errorMessageSize}));
	LocalFree(errorMessage);
}

void UsageMessage()
{
	Message("Usage"s,"Reheat.exe [game window title] [channel ID]"s);
}

void Quit()
{
	if (!PostThreadMessage(threadID,WM_QUIT,0,0)) throw std::runtime_error("This should never happen and prevent exiting the application");
}

std::optional<Json::Value> ParseMessage(const std::string &message)
{
	JSONCPP_STRING parseError;
	Json::Value json;
	Json::CharReaderBuilder builder;
	const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (!reader->parse(message.c_str(),message.c_str()+static_cast<int>(message.length()),&json,&parseError))
	{
		ErrorMessage(parseError);
		return std::nullopt;
	}
	return json;
}

std::optional<NormalizedPoint> Dispatch(const std::string &message)
{
	std::optional<Json::Value> payload=ParseMessage(message);
	if (!payload)
	{
		ErrorMessage("Failed to parse payload as JSON");
		return std::nullopt;
	}

	const std::string type=(*payload)[JSON_KEY_TYPE].asString();
	if (type == "system")
	{
		SystemMessage((*payload)[JSON_KEY_MESSAGE].asString());
		return std::nullopt;
	}
	if (type != "click")
	{
		SystemMessage(std::format("Unknown payload type ({})",type));
		return std::nullopt;
	}

	Json::Value x=(*payload)["x"];
	Json::Value y=(*payload)["y"];
	if (x.isNull() || y.isNull())
	{
		ErrorMessage("Payload is missing data"s);
		return std::nullopt;
	}
	return NormalizedPoint{std::stod(x.asString()),std::stod(y.asString())};
}

void ForwardClick(NormalizedPoint &normalizedPoint)
{
	window=FindWindowA(nullptr,targetWindowTitle.c_str());
	if (!window)
	{
		ErrorMessage("Window not found"s);
		return;
	}

	RECT dimensions={};
	GetClientRect(window,&dimensions);
	double clickX=dimensions.right*normalizedPoint.x;
	double clickY=dimensions.bottom*normalizedPoint.y;
	Message("Click"s,std::format("X: {}, Y: {}",std::to_string(clickX),std::to_string(clickY)));
	SetForegroundWindow(window);
	POINT screen={std::lrint(clickX),std::lrint(clickY)};
	ClientToScreen(window,&screen);
	SetCursorPos(screen.x,screen.y);
	INPUT inputs[2]={
		{
			.type=INPUT_MOUSE,
			.mi={
				.dx=screen.x,
				.dy=screen.y,
				.mouseData=0,
				.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTDOWN,
				.time=0
			}
		},
		{
			.type=INPUT_MOUSE,
			.mi={
				.dx=screen.x,
				.dy=screen.y,
				.mouseData=0,
				.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_LEFTUP,
				.time=0
			}
		}
	};
	SendInput(2,inputs,sizeof(INPUT));
}

VOID CALLBACK HTTPCallback(HINTERNET handle,DWORD_PTR context,DWORD status,LPVOID data,DWORD dataSize)
{
	switch (status)
	{
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
		PostThreadMessage(threadID,RM_HTTP_RECEIVE_DATA,0,0);
		break;
	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
		/*switch (static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError)
		{
		case ERROR_WINHTTP_CANNOT_CONNECT:
			qDebug() << "Can't connect";
			break;
		case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
			qDebug() << "Cert needed";
			break;
		case ERROR_WINHTTP_CONNECTION_ERROR:
			qDebug() << "Connection error";
			break;
		case ERROR_WINHTTP_INCORRECT_HANDLE_STATE:
			qDebug() << "Incorrect handle state";
			break;
		case ERROR_WINHTTP_INCORRECT_HANDLE_TYPE:
			qDebug() << "Incorrect handle type";
			break;
		default:
			qDebug() << "Unknown send error";
			break;
		}*/
		// FIXME: How do we report errors here?
		break;
	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
		PostThreadMessage(threadID,RM_WEBSOCKET_INITIALIZE,0,0);
		break;
	default:
		break;
	}
}

VOID CALLBACK WebSocketCallback(HINTERNET handle,DWORD_PTR context,DWORD status,LPVOID data,DWORD dataSize)
{
	switch (status)
	{
	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		WINHTTP_WEB_SOCKET_STATUS *payload=static_cast<WINHTTP_WEB_SOCKET_STATUS*>(data);
		if (payload->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
		{
			std::optional<NormalizedPoint> normalizedPoint=Dispatch(std::string{buffer,payload->dwBytesTransferred});
			if (normalizedPoint) ForwardClick(*normalizedPoint);
		}
		PostThreadMessage(threadID,RM_WEBSOCKET_PEEK,0,0);
		break;
	}
}

BOOL CALLBACK ConsoleCallback(DWORD controlSignal)
{
	SystemMessage("Control signal caught"s);
	switch (controlSignal)
	{
	case CTRL_C_EVENT:
		SystemMessage("Exit signal received"s);
		Quit();
		return TRUE;
	}

	return FALSE;
}

void Start()
{
	webSocketSession=WinHttpOpen(L"Reheat",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,NULL,NULL,WINHTTP_FLAG_ASYNC);
	if (!webSocketSession)
	{
		PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(SESSION_START_ERROR),GetLastError());
		return;
	}

	webSocketConnection=WinHttpConnect(webSocketSession,L"heat-api.j38.net",INTERNET_DEFAULT_HTTP_PORT,0);
	if (!webSocketConnection)
	{
		PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(CONNECTION_INITIALIZE_ERROR),GetLastError());
		return;
	}

	webSocketRequest=WinHttpOpenRequest(webSocketConnection,NULL,L"channel/523304124",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
	if (!webSocketRequest)
	{
		PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(CONNECTION_INITIATE_ERROR),GetLastError());
		return;
	}
	if (WinHttpSetStatusCallback(webSocketRequest,(WINHTTP_STATUS_CALLBACK)HTTPCallback,WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,NULL) == WINHTTP_INVALID_STATUS_CALLBACK)
	{
		PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(CONNECTION_INITIATE_ERROR),GetLastError());
		return;
	}

	if (!WinHttpSetOption(webSocketRequest,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0) || !WinHttpSendRequest(webSocketRequest,WINHTTP_NO_ADDITIONAL_HEADERS,0,NULL,0,0,0))
	{
		PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(CONNECTION_WEBSOCKET_ERROR),GetLastError());
		return;
	}
}

void CloseRequest()
{
	if (webSocketRequest)
	{
		if (WinHttpCloseHandle(webSocketRequest)) webSocketRequest=NULL;
	}
}

void Stop()
{
	// prevent additional callbacks from coming in as we're shutting down
	WinHttpSetStatusCallback(webSocketSession,NULL,WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,NULL);
	WinHttpSetStatusCallback(webSocket,NULL,WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,NULL);
	
	// TODO: more error handling
	CloseRequest();
	if (webSocket)
	{
		if (WinHttpWebSocketClose(webSocket,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,NULL,0) == ERROR_SUCCESS) webSocket=NULL; // TODO: make reason string a constant so we can calculate value programmatically // FIXME: how do I pass the reason?
	}
	if (webSocketConnection)
	{
		if (WinHttpCloseHandle(webSocketConnection)) webSocketConnection=NULL;
	}
	if (webSocketSession)
	{
		if (WinHttpCloseHandle(webSocketSession)) webSocketSession=NULL;
	}
}

int main(int argc,char *argv[])
{
	if (argc < 3)
	{
		if (argc < 2)
			ErrorMessage("Target window title and channel required"s);
		else
			ErrorMessage("Target channel required"s);
		UsageMessage();
		return static_cast<int>(ExitCode::INVALID);
	}

	targetWindowTitle=argv[1];
	targetTwitchChannelID=argv[2];
	threadID=GetCurrentThreadId();
	SetConsoleCtrlHandler(&ConsoleCallback,TRUE); // FIXME: error handling!
	PostThreadMessage(threadID,RM_START,0,0);

	MSG message;
	while (GetMessage(&message,static_cast<HWND>(INVALID_HANDLE_VALUE),0,0))
	{
		switch (message.message)
		{
		case RM_START:
			Start();
			break;
		case RM_HTTP_RECEIVE_DATA:
			// TODO: cast c++ way possible?
			if (!WinHttpReceiveResponse(webSocketRequest,0)) PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(WEBSOCKET_RECEIVE_FAILED),GetLastError()); // TODO: documentation says to check for a 101 HTTP response code here, but example doesn't show how to do that
			break;
		case RM_WEBSOCKET_INITIALIZE:
			webSocket=WinHttpWebSocketCompleteUpgrade(webSocketRequest,NULL);
			if (webSocket)
				PostThreadMessage(threadID,RM_WEBSOCKET_PEEK,0,0);
			else
				PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(WEBSOCKET_UPGRADE_FAILED),GetLastError());
			WinHttpSetStatusCallback(webSocket,static_cast<WINHTTP_STATUS_CALLBACK>(WebSocketCallback),WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,NULL);
			CloseRequest();
			break;
		case RM_WEBSOCKET_PEEK:
			if (DWORD result=WinHttpWebSocketReceive(webSocket,&buffer,1024,NULL,NULL); result != NO_ERROR)
			{
				switch (result)
				{
				case ERROR_INVALID_OPERATION:
					SystemMessage("Reading data..."s);
					continue;
				case ERROR_INVALID_PARAMETER:
					PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(WEBSOCKET_PEEK_INVALID_PARAMETER),0);
					continue;
				case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
					PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(WEBSOCKET_PEEK_INVALID_SERVER_RESPONSE),0);
					continue;
				case ERROR_WINHTTP_OPERATION_CANCELLED:
					PostThreadMessage(threadID,RM_SYSTEM_ERROR,reinterpret_cast<WPARAM>(WEBSOCKET_PEEK_CANCELLED),0);
					continue;
				}
			}
			PostThreadMessage(threadID,RM_WEBSOCKET_PEEK,0,0);
			break;
		case RM_SYSTEM_ERROR:
			WindowsErrorMessage(message);
			Quit();
			break;
		default:
			break;
		}
		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	return static_cast<int>(ExitCode::OK);
}