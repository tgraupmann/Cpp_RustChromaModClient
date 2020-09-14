// This is a project uses the following packages
//
// libCurl - HTTP GET
// jsoncpp - Constructing JSON
//

#include <array>
#include <chrono>
#include <conio.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <streambuf>
#include <string>
#include <time.h>
#include <thread>
#include <unordered_map>

#include <json/json.h>

#define CURL_STATICLIB
#include <curl.h>

typedef unsigned char byte;
#include "Razer\ChromaAnimationAPI.h"

using namespace ChromaSDK;
using namespace std;

static string _sServerHost = "localhost";
static string _sServerPort = "5000";
static bool _sWaitForExit = true;
static string _sSelectedPlayer = "";
static vector<string> _sPlayers;
static mutex _sPlayersMutex;
static mutex _sPlayAnimationMutex;
class DeviceFrameIndex
{
public:
	DeviceFrameIndex() {
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_ChromaLink] = -1;
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_Headset] = -1;
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_Keyboard] = -1;
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_Keypad] = -1;
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_Mouse] = -1;
		_mFrameIndex[(int)EChromaSDKDeviceEnum::DE_Mousepad] = -1;
	}
	// Index corresponds to EChromaSDKDeviceEnum;
	int _mFrameIndex[6];
};
static unordered_map<unsigned int, DeviceFrameIndex> _sFrameIndexes;

const float MATH_PI = 3.14159f;

// This final animation will have a single frame
// This animation will be in immediate mode to avoid any caching
// Any color changes will immediately display in the next frame update.
const char* ANIMATION_FINAL_CHROMA_LINK = "Dynamic\\Final_ChromaLink.chroma";
const char* ANIMATION_FINAL_HEADSET = "Dynamic\\Final_Headset.chroma";
const char* ANIMATION_FINAL_KEYBOARD = "Dynamic\\Final_Keyboard.chroma";
const char* ANIMATION_FINAL_KEYPAD = "Dynamic\\Final_Keypad.chroma";
const char* ANIMATION_FINAL_MOUSE = "Dynamic\\Final_Mouse.chroma";
const char* ANIMATION_FINAL_MOUSEPAD = "Dynamic\\Final_Mousepad.chroma";

// Function prototypes
void BlendAnimations(int* colorsChromaLink, int* tempColorsChromaLink,
	int* colorsHeadset, int* tempColorsHeadset,
	int* colorsKeyboard, int* tempColorsKeyboard,
	int* colorsKeypad, int* tempColorsKeypad,
	int* colorsMouse, int* tempColorsMouse,
	int* colorsMousepad, int* tempColorsMousepad);
void Cleanup();
void GameLoop();
int GetKeyColorIndex(int row, int column);
int GetSelectedPlayerIndex();
void HandleInput();
void HandleInputHost();
void HandleInputPort();
void HandleInputPlayer();
void HandleInputSelectPlayer();
void Init();
int main();
void PrintLegend();
void QueueAnimation(unsigned int index);
void ReadConfig();
void SetKeyColor(int* colors, int rzkey, int color);
void SetKeyColorRGB(int* colors, int rzkey, int red, int green, int blue);
void SetupAnimations();
void WriteConfig();

string GetAppData()
{
	char* pValue;
	size_t len;
	errno_t err = _dupenv_s(&pValue, &len, "APPDATA");
	if (err)
	{
		return "";
	}
	else
	{
		string result = pValue;
		return result;
	}
}

string GetConfigPath()
{
	string result = GetAppData();
	result += "\\RustChromaModClient";
	return result;
}

string GetConfigFilePath()
{
	string result = GetConfigPath();
	result += "\\config.json";
	return result;
}

void ReadConfig()
{
	string path = GetConfigPath();
	if (!filesystem::exists(path))
	{
		filesystem::create_directories(path);
	}

	string configFilePath = GetConfigFilePath();
	ifstream t(configFilePath);
	string str((istreambuf_iterator<char>(t)), istreambuf_iterator<char>());
	if (str.empty())
	{
		WriteConfig();
	}
	else
	{
		Json::Value root;
		Json::Reader reader;
		bool parsingSuccessful = reader.parse(str, root);
		if (parsingSuccessful)
		{
			for (Json::Value::const_iterator outer = root.begin(); outer != root.end(); ++outer)
			{
				Json::Value name = outer.key();
				string val = root[name.asCString()].asString();
				if (!strcmp(name.asCString(), "host"))
				{
					_sServerHost = val;
				}
				else if (!strcmp(name.asCString(), "port"))
				{
					_sServerPort = val;
				}
				else if (!strcmp(name.asCString(), "player"))
				{
					_sSelectedPlayer = val;
				}
			}
		}
	}
}

void WriteConfig()
{
	string path = GetConfigPath();
	if (!filesystem::exists(path))
	{
		filesystem::create_directories(path);
	}

	Json::Value json;
	json["host"] = _sServerHost;
	json["port"] = _sServerPort;
	json["player"] = _sSelectedPlayer;

	Json::FastWriter fastWriter;
	std::string strJson = fastWriter.write(json);

	string configFilePath = GetConfigFilePath();
	std::ofstream out(configFilePath);
	out << strJson;
	out.close();
}

size_t CurlWrite_CallbackFunc_StdString(void* contents, size_t size, size_t nmemb, string* s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char*)contents, newLength);
    }
    catch (std::bad_alloc&)
    {
        //handle memory problem
        return 0;
    }
    return newLength;
}

void GetServerPlayers()
{
	while (_sWaitForExit)
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
		auto curl = curl_easy_init();
		if (curl) {
			string url = "http://";
			url += _sServerHost;
			url += ":";
			url += _sServerPort;
			url += "/players.json";
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
			curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
			curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

			std::string response_string;
			std::string header_string;
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
			curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);

			curl_easy_perform(curl);
			//cout << "Response: players.json ===" << endl << response_string << endl;
			curl_easy_cleanup(curl);
			curl_global_cleanup();
			curl = NULL;

			lock_guard<mutex> guard(_sPlayersMutex);
			_sPlayers.clear();

			Json::Value root;
			Json::Reader reader;
			bool parsingSuccessful = reader.parse(response_string, root);
			if (parsingSuccessful)
			{
				for (unsigned int i = 0; i < root.size(); ++i)
				{
					_sPlayers.push_back(root[i].asString());
				}
			}

			PrintLegend();
		}
		Sleep(3000); // Update the players every few second
	}
}

void QueueAnimation(unsigned int effectIndex)
{
	lock_guard<mutex> guard(_sPlayAnimationMutex);
	DeviceFrameIndex deviceFrameIndex;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_ChromaLink)] = 0;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_Headset)] = 0;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_Keyboard)] = 0;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_Keypad)] = 0;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_Mouse)] = 0;
	deviceFrameIndex._mFrameIndex[int(EChromaSDKDeviceEnum::DE_Mousepad)] = 0;
	_sFrameIndexes[effectIndex] = deviceFrameIndex; // start
}

void GetServerPlayer()
{
	QueueAnimation(1); //play title animation at the start

	while (_sWaitForExit)
	{
		if (!_sSelectedPlayer.empty())
		{
			curl_global_init(CURL_GLOBAL_DEFAULT);
			auto curl = curl_easy_init();
			if (curl) {
				string url = "http://";
				url += _sServerHost;
				url += ":";
				url += _sServerPort;
				url += "/player.json?player=";

				char* encodedPlayer = curl_easy_escape(curl, _sSelectedPlayer.c_str(), _sSelectedPlayer.size());
				if (encodedPlayer) {
					url += encodedPlayer;
					curl_free(encodedPlayer);
				}

				curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
				curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
				curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
				curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

				std::string response_string;
				std::string header_string;
				curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
				curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
				curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);

				curl_easy_perform(curl);
				cout << "Response: player.json ===" << endl << response_string << endl;
				curl_easy_cleanup(curl);
				curl_global_cleanup();
				curl = NULL;

				lock_guard<mutex> guard(_sPlayersMutex);

				Json::Value root;
				Json::Reader reader;
				bool parsingSuccessful = reader.parse(response_string, root);
				if (parsingSuccessful)
				{
					for (unsigned int i = 0; i < root.size(); ++i)
					{
						Json::Value evt = root[i];

						string dataEvent = "";
						string dataMessage = "";
						for (Json::Value::const_iterator outer = evt.begin(); outer != evt.end(); ++outer)
						{
							Json::Value name = outer.key();
							string val = evt[name.asCString()].asString();
							if (!strcmp(name.asCString(), "event"))
							{
								dataEvent = val;
							}
							else if (!strcmp(name.asCString(), "message"))
							{
								dataMessage = val;
							}
						}

						if (!dataEvent.empty())
						{
							cout << "Player Event: event=" << dataEvent << endl;
							if (!strcmp(dataEvent.c_str(), "OnPlayerAttack"))
							{
								string hitEntity = evt["hit_entity"].asString();
								if (!strcmp(hitEntity.c_str(), "TreeEntity"))
								{
									QueueAnimation(8);
								}
							}
							else if (!strcmp(dataEvent.c_str(), "OnActiveItemChanged"))
							{
								QueueAnimation(17);
							}
							else if (!strcmp(dataEvent.c_str(), "OnMessagePlayer"))
							{
								if (!strcmp(dataMessage.c_str(), "Can't afford to place!"))
								{
									QueueAnimation(18);
								}
							}
							else if (!strcmp(dataEvent.c_str(), "OnMeleeThrown"))
							{
								QueueAnimation(19);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerJump"))
							{
								QueueAnimation(20);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerDuck"))
							{
								QueueAnimation(21);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerSprint"))
							{
								QueueAnimation(22);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerConnected"))
							{
								QueueAnimation(1);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerDisconnected"))
							{
								QueueAnimation(1);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerDeath"))
							{
								QueueAnimation(24);
							}
						}
					}
				}
			}
		}
		Sleep(100); //Get the player events 10 times a second
	}
}


void Init()
{
	if (ChromaAnimationAPI::InitAPI() != 0)
	{
		cerr << "Failed to load Chroma library!" << endl;
		exit(1);
	}
	RZRESULT result = ChromaAnimationAPI::Init();
	if (result != RZRESULT_SUCCESS)
	{
		cerr << "Failed to initialize Chroma!" << endl;
		exit(1);
	}
	Sleep(100); //wait for init
}


void SetupEvent(const char* baseLayer, int effectIndex)
{
	string animationName = "Event\\Effect";
	animationName += to_string(effectIndex);

	int deviceType = ChromaAnimationAPI::GetDeviceTypeName(baseLayer);
	int device = ChromaAnimationAPI::GetDeviceName(baseLayer);
	switch ((EChromaSDKDeviceTypeEnum)deviceType)
	{
	case EChromaSDKDeviceTypeEnum::DE_1D:
		switch ((EChromaSDKDevice1DEnum)device)
		{
		case EChromaSDKDevice1DEnum::DE_ChromaLink:
			animationName += "_ChromaLink.chroma";
			break;
		case EChromaSDKDevice1DEnum::DE_Headset:
			animationName += "_Headset.chroma";
			break;
		case EChromaSDKDevice1DEnum::DE_Mousepad:
			animationName += "_Mousepad.chroma";
			break;
		}
		break;
	case EChromaSDKDeviceTypeEnum::DE_2D:
		switch ((EChromaSDKDevice2DEnum)device)
		{
		case EChromaSDKDevice2DEnum::DE_Keyboard:
			animationName += "_Keyboard.chroma";
			break;
		case EChromaSDKDevice2DEnum::DE_Keypad:
			animationName += "_Keypad.chroma";
			break;
		case EChromaSDKDevice2DEnum::DE_Mouse:
			animationName += "_Mouse.chroma";
			break;
		}
		break;
	}

	ChromaAnimationAPI::CopyAnimationName(baseLayer, animationName.c_str());
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
}

#pragma region Autogenerated
void SetupEffect1Keyboard()
{
	const char* baseLayer = "Animations/Blank_Keyboard.chroma";
	const char* layer2 = "Animations/Title_Keyboard.chroma";
	const char* layer3 = "Animations/BlackAndWhiteRainbow_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::CloseAnimationName(layer3);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	ChromaAnimationAPI::GetAnimation(layer3);
	ChromaAnimationAPI::ReduceFramesName(layer2, 2);
	int frameCount = ChromaAnimationAPI::GetFrameCountName(layer2);
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(layer3, color1, color2);
	ChromaAnimationAPI::CopyNonZeroTargetAllKeysAllFramesName(layer3, layer2);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 25, 37, 50);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1ChromaLink()
{
	const char* baseLayer = "Animations/BlackAndWhiteRainbow_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Headset()
{
	const char* baseLayer = "Animations/BlackAndWhiteRainbow_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Mousepad()
{
	const char* baseLayer = "Animations/BlackAndWhiteRainbow_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Mouse()
{
	const char* baseLayer = "Animations/BlackAndWhiteRainbow_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Keypad()
{
	const char* baseLayer = "Animations/BlackAndWhiteRainbow_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 128, 128);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect2Keyboard()
{
	const char* baseLayer = "Animations/Effect2_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2ChromaLink()
{
	const char* baseLayer = "Animations/Effect2_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Headset()
{
	const char* baseLayer = "Animations/Effect2_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Mousepad()
{
	const char* baseLayer = "Animations/Effect2_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Mouse()
{
	const char* baseLayer = "Animations/Effect2_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Keypad()
{
	const char* baseLayer = "Animations/Effect2_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(198, 227, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 128, 255);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 27, 38, 50);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect3Keyboard()
{
	const char* baseLayer = "Animations/Effect3_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3ChromaLink()
{
	const char* baseLayer = "Animations/Effect3_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Headset()
{
	const char* baseLayer = "Animations/Effect3_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Mousepad()
{
	const char* baseLayer = "Animations/Effect3_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Mouse()
{
	const char* baseLayer = "Animations/Effect3_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Keypad()
{
	const char* baseLayer = "Animations/Effect3_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 215, 0);
	int color2 = ChromaAnimationAPI::GetRGB(132, 81, 63);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 46, 46);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect4Keyboard()
{
	const char* baseLayer = "Animations/Effect4_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect4ChromaLink()
{
	const char* baseLayer = "Animations/Effect4_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Headset()
{
	const char* baseLayer = "Animations/Effect4_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Mousepad()
{
	const char* baseLayer = "Animations/Effect4_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Mouse()
{
	const char* baseLayer = "Animations/Effect4_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Keypad()
{
	const char* baseLayer = "Animations/Effect4_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(75, 6, 6);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 50, 47, 47);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 4);
}
void SetupEffect5Keyboard()
{
	const char* baseLayer = "Animations/Effect5_Keyboard.chroma";
	const char* layer2 = "Animations/RockMelee_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5ChromaLink()
{
	const char* baseLayer = "Animations/Effect5_ChromaLink.chroma";
	const char* layer2 = "Animations/RockMelee_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Headset()
{
	const char* baseLayer = "Animations/Effect5_Headset.chroma";
	const char* layer2 = "Animations/RockMelee_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Mousepad()
{
	const char* baseLayer = "Animations/Effect5_Mousepad.chroma";
	const char* layer2 = "Animations/RockMelee_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Mouse()
{
	const char* baseLayer = "Animations/Effect5_Mouse.chroma";
	const char* layer2 = "Animations/RockMelee_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Keypad()
{
	const char* baseLayer = "Animations/Effect5_Keypad.chroma";
	const char* layer2 = "Animations/RockMelee_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect6Keyboard()
{
	const char* baseLayer = "Animations/Effect6_Keyboard.chroma";
	const char* layer2 = "Animations/RockMelee_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6ChromaLink()
{
	const char* baseLayer = "Animations/Effect6_ChromaLink.chroma";
	const char* layer2 = "Animations/RockMelee_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Headset()
{
	const char* baseLayer = "Animations/Effect6_Headset.chroma";
	const char* layer2 = "Animations/RockMelee_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Mousepad()
{
	const char* baseLayer = "Animations/Effect6_Mousepad.chroma";
	const char* layer2 = "Animations/RockMelee_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Mouse()
{
	const char* baseLayer = "Animations/Effect6_Mouse.chroma";
	const char* layer2 = "Animations/RockMelee_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Keypad()
{
	const char* baseLayer = "Animations/Effect6_Keypad.chroma";
	const char* layer2 = "Animations/RockMelee_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color4 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect7Keyboard()
{
	const char* baseLayer = "Animations/Effect6_Keyboard.chroma";
	const char* layer2 = "Animations/Axe_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7ChromaLink()
{
	const char* baseLayer = "Animations/Effect6_ChromaLink.chroma";
	const char* layer2 = "Animations/Axe_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Headset()
{
	const char* baseLayer = "Animations/Effect6_Headset.chroma";
	const char* layer2 = "Animations/Axe_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Mousepad()
{
	const char* baseLayer = "Animations/Effect6_Mousepad.chroma";
	const char* layer2 = "Animations/Axe_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Mouse()
{
	const char* baseLayer = "Animations/Effect6_Mouse.chroma";
	const char* layer2 = "Animations/Axe_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Keypad()
{
	const char* baseLayer = "Animations/Effect6_Keypad.chroma";
	const char* layer2 = "Animations/Axe_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(166, 167, 166);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect8Keyboard()
{
	const char* baseLayer = "Animations/Effect8_Keyboard.chroma";
	const char* layer2 = "Animations/Axe_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8ChromaLink()
{
	const char* baseLayer = "Animations/Effect8_ChromaLink.chroma";
	const char* layer2 = "Animations/Axe_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Headset()
{
	const char* baseLayer = "Animations/Effect8_Headset.chroma";
	const char* layer2 = "Animations/Axe_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Mousepad()
{
	const char* baseLayer = "Animations/Effect8_Mousepad.chroma";
	const char* layer2 = "Animations/Axe_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Mouse()
{
	const char* baseLayer = "Animations/Effect8_Mouse.chroma";
	const char* layer2 = "Animations/Axe_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Keypad()
{
	const char* baseLayer = "Animations/Effect8_Keypad.chroma";
	const char* layer2 = "Animations/Axe_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::CloseAnimationName(layer2);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::GetAnimation(layer2);
	int color1 = ChromaAnimationAPI::GetRGB(118, 255, 255);
	int color2 = ChromaAnimationAPI::GetRGB(0, 0, 127);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	int color3 = ChromaAnimationAPI::GetRGB(53, 5, 5);
	int color4 = ChromaAnimationAPI::GetRGB(170, 170, 97);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(layer2, color3, color4);
	ChromaAnimationAPI::CopyNonZeroAllKeysAllFramesName(layer2, baseLayer);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect9Keyboard()
{
	const char* baseLayer = "Animations/Effect9_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9ChromaLink()
{
	const char* baseLayer = "Animations/Effect9_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Headset()
{
	const char* baseLayer = "Animations/Effect9_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Mousepad()
{
	const char* baseLayer = "Animations/Effect9_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Mouse()
{
	const char* baseLayer = "Animations/Effect9_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Keypad()
{
	const char* baseLayer = "Animations/Effect9_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect10Keyboard()
{
	const char* baseLayer = "Animations/Effect10_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 10);
}
void SetupEffect10ChromaLink()
{
	const char* baseLayer = "Animations/Effect10_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	SetupEvent(baseLayer, 10);
}
void SetupEffect10Headset()
{
	const char* baseLayer = "Animations/Effect10_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	SetupEvent(baseLayer, 10);
}
void SetupEffect10Mousepad()
{
	const char* baseLayer = "Animations/Effect10_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	SetupEvent(baseLayer, 10);
}
void SetupEffect10Mouse()
{
	const char* baseLayer = "Animations/Effect10_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	SetupEvent(baseLayer, 10);
}
void SetupEffect10Keypad()
{
	const char* baseLayer = "Animations/Effect10_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(255, 69, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 149);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::FillZeroColorAllFramesRGBName(baseLayer, 1, 47, 92);
	SetupEvent(baseLayer, 10);
}
void SetupEffect11Keyboard()
{
	const char* baseLayer = "Animations/Effect11_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect11ChromaLink()
{
	const char* baseLayer = "Animations/Effect11_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect11Headset()
{
	const char* baseLayer = "Animations/Effect11_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect11Mousepad()
{
	const char* baseLayer = "Animations/Effect11_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect11Mouse()
{
	const char* baseLayer = "Animations/Effect11_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect11Keypad()
{
	const char* baseLayer = "Animations/Effect11_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(120, 120, 120);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 11);
}
void SetupEffect12Keyboard()
{
	const char* baseLayer = "Animations/Effect12_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect12ChromaLink()
{
	const char* baseLayer = "Animations/Effect12_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect12Headset()
{
	const char* baseLayer = "Animations/Effect12_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect12Mousepad()
{
	const char* baseLayer = "Animations/Effect12_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect12Mouse()
{
	const char* baseLayer = "Animations/Effect12_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect12Keypad()
{
	const char* baseLayer = "Animations/Effect12_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(103, 43, 20);
	int color2 = ChromaAnimationAPI::GetRGB(173, 173, 164);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 12);
}
void SetupEffect13Keyboard()
{
	const char* baseLayer = "Animations/Effect13_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect13ChromaLink()
{
	const char* baseLayer = "Animations/Effect13_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect13Headset()
{
	const char* baseLayer = "Animations/Effect13_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect13Mousepad()
{
	const char* baseLayer = "Animations/Effect13_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect13Mouse()
{
	const char* baseLayer = "Animations/Effect13_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect13Keypad()
{
	const char* baseLayer = "Animations/Effect13_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(71, 24, 24);
	int color2 = ChromaAnimationAPI::GetRGB(214, 116, 79);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 13);
}
void SetupEffect14Keyboard()
{
	const char* baseLayer = "Animations/Effect14_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect14ChromaLink()
{
	const char* baseLayer = "Animations/Effect14_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect14Headset()
{
	const char* baseLayer = "Animations/Effect14_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect14Mousepad()
{
	const char* baseLayer = "Animations/Effect14_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect14Mouse()
{
	const char* baseLayer = "Animations/Effect14_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect14Keypad()
{
	const char* baseLayer = "Animations/Effect14_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	ChromaAnimationAPI::ReduceFramesName(baseLayer, 2);
	int color1 = ChromaAnimationAPI::GetRGB(147, 81, 0);
	int color2 = ChromaAnimationAPI::GetRGB(153, 153, 138);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 14);
}
void SetupEffect15Keyboard()
{
	const char* baseLayer = "Animations/Effect15_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect15ChromaLink()
{
	const char* baseLayer = "Animations/Effect15_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect15Headset()
{
	const char* baseLayer = "Animations/Effect15_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect15Mousepad()
{
	const char* baseLayer = "Animations/Effect15_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect15Mouse()
{
	const char* baseLayer = "Animations/Effect15_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect15Keypad()
{
	const char* baseLayer = "Animations/Effect15_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(163, 13, 13);
	int color2 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	ChromaAnimationAPI::MultiplyNonZeroTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 15);
}
void SetupEffect16Keyboard() {
	const char* baseLayer = "Animations/Effect16_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect16ChromaLink() {
	const char* baseLayer = "Animations/Effect16_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect16Headset() {
	const char* baseLayer = "Animations/Effect16_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect16Mousepad() {
	const char* baseLayer = "Animations/Effect16_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect16Mouse() {
	const char* baseLayer = "Animations/Effect16_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect16Keypad() {
	const char* baseLayer = "Animations/Effect16_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 16);
}
void SetupEffect17Keyboard() {
	const char* baseLayer = "Animations/Effect17_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect17ChromaLink() {
	const char* baseLayer = "Animations/Effect17_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect17Headset() {
	const char* baseLayer = "Animations/Effect17_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect17Mousepad() {
	const char* baseLayer = "Animations/Effect17_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect17Mouse() {
	const char* baseLayer = "Animations/Effect17_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect17Keypad() {
	const char* baseLayer = "Animations/Effect17_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 17);
}
void SetupEffect18Keyboard() {
	const char* baseLayer = "Animations/Blank_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect18ChromaLink() {
	const char* baseLayer = "Animations/Blank_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect18Headset() {
	const char* baseLayer = "Animations/Blank_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect18Mousepad() {
	const char* baseLayer = "Animations/Blank_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect18Mouse() {
	const char* baseLayer = "Animations/Blank_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect18Keypad() {
	const char* baseLayer = "Animations/Blank_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 18);
}
void SetupEffect19Keyboard() {
	const char* baseLayer = "Animations/Blank_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.033f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect19ChromaLink() {
	const char* baseLayer = "Animations/Blank_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect19Headset() {
	const char* baseLayer = "Animations/Blank_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect19Mousepad() {
	const char* baseLayer = "Animations/Blank_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect19Mouse() {
	const char* baseLayer = "Animations/Blank_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect19Keypad() {
	const char* baseLayer = "Animations/Blank_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 8;
	ChromaAnimationAPI::MakeBlankFramesName(baseLayer, frameCount, 0.1f, 0);
	ChromaAnimationAPI::FillRandomColorsBlackAndWhiteAllFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	ChromaAnimationAPI::DuplicateFramesName(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 19);
}
void SetupEffect20Keyboard() {
	const char* baseLayer = "Animations/Blank_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect20ChromaLink() {
	const char* baseLayer = "Animations/Blank_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect20Headset() {
	const char* baseLayer = "Animations/Blank_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect20Mousepad() {
	const char* baseLayer = "Animations/Blank_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect20Mouse() {
	const char* baseLayer = "Animations/Blank_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect20Keypad() {
	const char* baseLayer = "Animations/Blank_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 20);
}
void SetupEffect21Keyboard() {
	const char* baseLayer = "Animations/Effect21_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect21ChromaLink() {
	const char* baseLayer = "Animations/Effect21_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect21Headset() {
	const char* baseLayer = "Animations/Effect21_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect21Mousepad() {
	const char* baseLayer = "Animations/Effect21_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect21Mouse() {
	const char* baseLayer = "Animations/Effect21_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect21Keypad() {
	const char* baseLayer = "Animations/Effect21_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 21);
}
void SetupEffect22Keyboard() {
	const char* baseLayer = "Animations/Effect22_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect22ChromaLink() {
	const char* baseLayer = "Animations/Effect22_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect22Headset() {
	const char* baseLayer = "Animations/Effect22_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect22Mousepad() {
	const char* baseLayer = "Animations/Effect22_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect22Mouse() {
	const char* baseLayer = "Animations/Effect22_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect22Keypad() {
	const char* baseLayer = "Animations/Effect22_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 22);
}
void SetupEffect23Keyboard() {
	const char* baseLayer = "Animations/Effect23_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect23ChromaLink() {
	const char* baseLayer = "Animations/Effect23_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect23Headset() {
	const char* baseLayer = "Animations/Effect23_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect23Mousepad() {
	const char* baseLayer = "Animations/Effect23_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect23Mouse() {
	const char* baseLayer = "Animations/Effect23_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect23Keypad() {
	const char* baseLayer = "Animations/Effect23_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 23);
}
void SetupEffect24Keyboard() {
	const char* baseLayer = "Animations/Effect24_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
void SetupEffect24ChromaLink() {
	const char* baseLayer = "Animations/Effect24_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
void SetupEffect24Headset() {
	const char* baseLayer = "Animations/Effect24_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
void SetupEffect24Mousepad() {
	const char* baseLayer = "Animations/Effect24_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
void SetupEffect24Mouse() {
	const char* baseLayer = "Animations/Effect24_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
void SetupEffect24Keypad() {
	const char* baseLayer = "Animations/Effect24_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 24);
}
#pragma endregion


void SetupAnimations()
{
	// Create a blank animation in memory
	int animationIdChromaLink = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_1D, (int)EChromaSDKDevice1DEnum::DE_ChromaLink);
	int animationIdHeadset = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_1D, (int)EChromaSDKDevice1DEnum::DE_Headset);
	int animationIdKeyboard = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_2D, (int)EChromaSDKDevice2DEnum::DE_Keyboard);
	int animationIdKeypad = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_2D, (int)EChromaSDKDevice2DEnum::DE_Keypad);
	int animationIdMouse = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_2D, (int)EChromaSDKDevice2DEnum::DE_Mouse);
	int animationIdMousepad = ChromaAnimationAPI::CreateAnimationInMemory((int)EChromaSDKDeviceTypeEnum::DE_1D, (int)EChromaSDKDevice1DEnum::DE_Mousepad);

	// Copy animation in memory to named animation
	ChromaAnimationAPI::CopyAnimation(animationIdChromaLink, ANIMATION_FINAL_CHROMA_LINK);
	ChromaAnimationAPI::CopyAnimation(animationIdHeadset, ANIMATION_FINAL_HEADSET);
	ChromaAnimationAPI::CopyAnimation(animationIdKeyboard, ANIMATION_FINAL_KEYBOARD);
	ChromaAnimationAPI::CopyAnimation(animationIdKeypad, ANIMATION_FINAL_KEYPAD);
	ChromaAnimationAPI::CopyAnimation(animationIdMouse, ANIMATION_FINAL_MOUSE);
	ChromaAnimationAPI::CopyAnimation(animationIdMousepad, ANIMATION_FINAL_MOUSEPAD);

	// Close temp animation
	ChromaAnimationAPI::CloseAnimation(animationIdChromaLink);
	ChromaAnimationAPI::CloseAnimation(animationIdHeadset);
	ChromaAnimationAPI::CloseAnimation(animationIdKeyboard);
	ChromaAnimationAPI::CloseAnimation(animationIdKeypad);
	ChromaAnimationAPI::CloseAnimation(animationIdMouse);
	ChromaAnimationAPI::CloseAnimation(animationIdMousepad);

	#pragma region Setup effects

	SetupEffect1ChromaLink();
	SetupEffect1Headset();
	SetupEffect1Keyboard();
	SetupEffect1Keypad();
	SetupEffect1Mousepad();
	SetupEffect1Mouse();

	SetupEffect2ChromaLink();
	SetupEffect2Headset();
	SetupEffect2Keyboard();
	SetupEffect2Keypad();
	SetupEffect2Mousepad();
	SetupEffect2Mouse();

	SetupEffect3ChromaLink();
	SetupEffect3Headset();
	SetupEffect3Keyboard();
	SetupEffect3Keypad();
	SetupEffect3Mousepad();
	SetupEffect3Mouse();

	SetupEffect4ChromaLink();
	SetupEffect4Headset();
	SetupEffect4Keyboard();
	SetupEffect4Keypad();
	SetupEffect4Mousepad();
	SetupEffect4Mouse();

	SetupEffect5ChromaLink();
	SetupEffect5Headset();
	SetupEffect5Keyboard();
	SetupEffect5Keypad();
	SetupEffect5Mousepad();
	SetupEffect5Mouse();

	SetupEffect6ChromaLink();
	SetupEffect6Headset();
	SetupEffect6Keyboard();
	SetupEffect6Keypad();
	SetupEffect6Mousepad();
	SetupEffect6Mouse();

	SetupEffect7ChromaLink();
	SetupEffect7Headset();
	SetupEffect7Keyboard();
	SetupEffect7Keypad();
	SetupEffect7Mousepad();
	SetupEffect7Mouse();

	SetupEffect8ChromaLink();
	SetupEffect8Headset();
	SetupEffect8Keyboard();
	SetupEffect8Keypad();
	SetupEffect8Mousepad();
	SetupEffect8Mouse();

	SetupEffect9ChromaLink();
	SetupEffect9Headset();
	SetupEffect9Keyboard();
	SetupEffect9Keypad();
	SetupEffect9Mousepad();
	SetupEffect9Mouse();

	SetupEffect10ChromaLink();
	SetupEffect10Headset();
	SetupEffect10Keyboard();
	SetupEffect10Keypad();
	SetupEffect10Mousepad();
	SetupEffect10Mouse();

	SetupEffect11ChromaLink();
	SetupEffect11Headset();
	SetupEffect11Keyboard();
	SetupEffect11Keypad();
	SetupEffect11Mousepad();
	SetupEffect11Mouse();

	SetupEffect12ChromaLink();
	SetupEffect12Headset();
	SetupEffect12Keyboard();
	SetupEffect12Keypad();
	SetupEffect12Mousepad();
	SetupEffect12Mouse();

	SetupEffect13ChromaLink();
	SetupEffect13Headset();
	SetupEffect13Keyboard();
	SetupEffect13Keypad();
	SetupEffect13Mousepad();
	SetupEffect13Mouse();

	SetupEffect14ChromaLink();
	SetupEffect14Headset();
	SetupEffect14Keyboard();
	SetupEffect14Keypad();
	SetupEffect14Mousepad();
	SetupEffect14Mouse();

	SetupEffect15ChromaLink();
	SetupEffect15Headset();
	SetupEffect15Keyboard();
	SetupEffect15Keypad();
	SetupEffect15Mousepad();
	SetupEffect15Mouse();

	SetupEffect16ChromaLink();
	SetupEffect16Headset();
	SetupEffect16Keyboard();
	SetupEffect16Keypad();
	SetupEffect16Mouse();
	SetupEffect16Mousepad();

	SetupEffect17ChromaLink();
	SetupEffect17Headset();
	SetupEffect17Keyboard();
	SetupEffect17Keypad();
	SetupEffect17Mouse();
	SetupEffect17Mousepad();

	SetupEffect18ChromaLink();
	SetupEffect18Headset();
	SetupEffect18Keyboard();
	SetupEffect18Keypad();
	SetupEffect18Mouse();
	SetupEffect18Mousepad();

	SetupEffect19ChromaLink();
	SetupEffect19Headset();
	SetupEffect19Keyboard();
	SetupEffect19Keypad();
	SetupEffect19Mouse();
	SetupEffect19Mousepad();

	SetupEffect20ChromaLink();
	SetupEffect20Headset();
	SetupEffect20Keyboard();
	SetupEffect20Keypad();
	SetupEffect20Mouse();
	SetupEffect20Mousepad();

	SetupEffect21ChromaLink();
	SetupEffect21Headset();
	SetupEffect21Keyboard();
	SetupEffect21Keypad();
	SetupEffect21Mouse();
	SetupEffect21Mousepad();

	SetupEffect22ChromaLink();
	SetupEffect22Headset();
	SetupEffect22Keyboard();
	SetupEffect22Keypad();
	SetupEffect22Mouse();
	SetupEffect22Mousepad();

	SetupEffect23ChromaLink();
	SetupEffect23Headset();
	SetupEffect23Keyboard();
	SetupEffect23Keypad();
	SetupEffect23Mouse();
	SetupEffect23Mousepad();

	SetupEffect24ChromaLink();
	SetupEffect24Headset();
	SetupEffect24Keyboard();
	SetupEffect24Keypad();
	SetupEffect24Mouse();
	SetupEffect24Mousepad();

	#pragma endregion Setup effects
}

int GetKeyColorIndex(int row, int column)
{
	return Keyboard::MAX_COLUMN * row + column;
}

void SetKeyColor(int* colors, int rzkey, int color)
{
	int row = HIBYTE(rzkey);
	int column = LOBYTE(rzkey);
	colors[GetKeyColorIndex(row, column)] = color;
}

void SetKeyColorRGB(int* colors, int rzkey, int red, int green, int blue)
{
	SetKeyColor(colors, rzkey, ChromaAnimationAPI::GetRGB(red, green, blue));
}

//ref: https://stackoverflow.com/questions/10905892/equivalent-of-gettimeday-for-windows
int gettimeofday(struct timeval* tp, struct timezone* tzp) {
	namespace sc = std::chrono;
	sc::system_clock::duration d = sc::system_clock::now().time_since_epoch();
	sc::seconds s = sc::duration_cast<sc::seconds>(d);
	tp->tv_sec = (long)s.count();
	tp->tv_usec = (long)sc::duration_cast<sc::microseconds>(d - s).count();

	return 0;
}


const int GetColorArraySize1D(EChromaSDKDevice1DEnum device)
{
	const int maxLeds = ChromaAnimationAPI::GetMaxLeds((int)device);
	return maxLeds;
}

const int GetColorArraySize2D(EChromaSDKDevice2DEnum device)
{
	const int maxRow = ChromaAnimationAPI::GetMaxRow((int)device);
	const int maxColumn = ChromaAnimationAPI::GetMaxColumn((int)device);
	return maxRow * maxColumn;
}

void BlendAnimation1D(DeviceFrameIndex& deviceFrameIndex, int device, EChromaSDKDevice1DEnum device1d, const char* animationName,
	int* colors, int* tempColors)
{
	const int size = GetColorArraySize1D(device1d);
	const int frameId = deviceFrameIndex._mFrameIndex[device];
	const int frameCount = ChromaAnimationAPI::GetFrameCountName(animationName);
	if (frameId < frameCount)
	{
		ChromaAnimationAPI::SetCurrentFrameName(animationName, frameId);
		//cout << animationName << ": " << (1 + ChromaAnimationAPI::GetCurrentFrameName(animationName)) << " of " << frameCount << endl;
		float duration;
		int animationId = ChromaAnimationAPI::GetAnimation(animationName);
		ChromaAnimationAPI::GetFrame(animationId, frameId, &duration, tempColors, size);
		for (int i = 0; i < size; ++i)
		{
			if (tempColors[i] != 0)
			{
				colors[i] = tempColors[i];
			}
		}
		deviceFrameIndex._mFrameIndex[device] = frameId + 1;
	}
	else
	{
		deviceFrameIndex._mFrameIndex[device] = -1;
	}
}

void BlendAnimation2D(DeviceFrameIndex& deviceFrameIndex, int device, EChromaSDKDevice2DEnum device2D, const char* animationName,
	int* colors, int* tempColors)
{
	const int size = GetColorArraySize2D(device2D);
	const int frameId = deviceFrameIndex._mFrameIndex[device];
	const int frameCount = ChromaAnimationAPI::GetFrameCountName(animationName);
	if (frameId < frameCount)
	{
		ChromaAnimationAPI::SetCurrentFrameName(animationName, frameId);
		//cout << animationName << ": " << (1 + ChromaAnimationAPI::GetCurrentFrameName(animationName)) << " of " << frameCount << endl;
		float duration;
		int animationId = ChromaAnimationAPI::GetAnimation(animationName);
		ChromaAnimationAPI::GetFrame(animationId, frameId, &duration, tempColors, size);
		for (int i = 0; i < size; ++i)
		{
			if (tempColors[i] != 0)
			{
				colors[i] = tempColors[i];
			}
		}
		deviceFrameIndex._mFrameIndex[device] = frameId + 1;
	}
	else
	{
		deviceFrameIndex._mFrameIndex[device] = -1;
	}
}

void SetAmbientColor1D(EChromaSDKDevice1DEnum device, int* colors, int ambientColor)
{
	const int size = GetColorArraySize1D(device);
	for (int i = 0; i < size; ++i)
	{
		if (colors[i] == 0)
		{
			colors[i] = ambientColor;
		}
	}
}

void SetAmbientColor2D(EChromaSDKDevice2DEnum device, int* colors, int ambientColor)
{
	const int size = GetColorArraySize2D(device);
	for (int i = 0; i < size; ++i)
	{
		if (colors[i] == 0)
		{
			colors[i] = ambientColor;
		}
	}
}


void BlendAnimations(int* colorsChromaLink, int* tempColorsChromaLink,
	int* colorsHeadset, int* tempColorsHeadset,
	int* colorsKeyboard, int* tempColorsKeyboard, 
	int* colorsKeypad, int* tempColorsKeypad, 
	int* colorsMouse, int* tempColorsMouse, 
	int* colorsMousepad, int* tempColorsMousepad)
{
	lock_guard<mutex> guard(_sPlayAnimationMutex);

	// blend active animations
	for (pair<const unsigned int, DeviceFrameIndex> pair : _sFrameIndexes)
	{
		DeviceFrameIndex deviceFrameIndex = pair.second;
		
		//iterate all device types
		for (int d = (int)EChromaSDKDeviceEnum::DE_ChromaLink; d < (int)EChromaSDKDeviceEnum::DE_MAX; ++d)
		{
			if (deviceFrameIndex._mFrameIndex[d] >= 0)
			{
				string animationName = "Event\\Effect";
				animationName += to_string(pair.first);

				switch ((EChromaSDKDeviceEnum)d)
				{
				case EChromaSDKDeviceEnum::DE_ChromaLink:
					animationName += "_ChromaLink.chroma";
					BlendAnimation1D(deviceFrameIndex, d, EChromaSDKDevice1DEnum::DE_ChromaLink, animationName.c_str(), colorsChromaLink, tempColorsChromaLink);
					break;
				case EChromaSDKDeviceEnum::DE_Headset:
					animationName += "_Headset.chroma";
					BlendAnimation1D(deviceFrameIndex, d, EChromaSDKDevice1DEnum::DE_Headset, animationName.c_str(), colorsHeadset, tempColorsHeadset);
					break;
				case EChromaSDKDeviceEnum::DE_Keyboard:
					animationName += "_Keyboard.chroma";
					BlendAnimation2D(deviceFrameIndex, d, EChromaSDKDevice2DEnum::DE_Keyboard, animationName.c_str(), colorsKeyboard, tempColorsKeyboard);
					break;
				case EChromaSDKDeviceEnum::DE_Keypad:
					animationName += "_Keypad.chroma";
					BlendAnimation2D(deviceFrameIndex, d, EChromaSDKDevice2DEnum::DE_Keypad, animationName.c_str(), colorsKeypad, tempColorsKeypad);
					break;
				case EChromaSDKDeviceEnum::DE_Mouse:
					animationName += "_Mouse.chroma";
					BlendAnimation2D(deviceFrameIndex, d, EChromaSDKDevice2DEnum::DE_Mouse, animationName.c_str(), colorsMouse, tempColorsMouse);
					break;
				case EChromaSDKDeviceEnum::DE_Mousepad:
					animationName += "_Mousepad.chroma";
					BlendAnimation1D(deviceFrameIndex, d, EChromaSDKDevice1DEnum::DE_Mousepad, animationName.c_str(), colorsMousepad, tempColorsMousepad);
					break;
				}
				_sFrameIndexes[pair.first] = deviceFrameIndex;
			}
		}
	}
}

void SetupKeyboardHotkeys(int* colorsKeyboard)
{
	// get time
	struct timeval tp;
	gettimeofday(&tp, NULL);
	long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;

	int color1 = ChromaAnimationAPI::GetRGB(255, 105, 20);
	int color2 = ChromaAnimationAPI::GetRGB(64, 16, 0);
	float t = fabsf(cos(MATH_PI / 2.0f + ms * 0.001f));
	int color = ChromaAnimationAPI::LerpColor(color1, color2, t);

	// Show hotkeys
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_W, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_A, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_S, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_D, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_LSHIFT, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_LCTRL, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_LALT, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_SPACE, color);

	color1 = ChromaAnimationAPI::GetRGB(0, 48, 48);
	color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	color = ChromaAnimationAPI::LerpColor(color1, color2, t);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_F1, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_G, color);

	color1 = ChromaAnimationAPI::GetRGB(0, 92, 0);
	color2 = ChromaAnimationAPI::GetRGB(0, 255, 0);
	color = ChromaAnimationAPI::LerpColor(color1, color2, t);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_E, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_N, color);

	color1 = ChromaAnimationAPI::GetRGB(255, 255, 255);
	color2 = ChromaAnimationAPI::GetRGB(48, 48, 48);
	color = ChromaAnimationAPI::LerpColor(color1, color2, t);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_1, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_2, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_3, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_4, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_5, color);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY::RZKEY_6, color);

	color1 = ChromaAnimationAPI::GetRGB(48, 0, 0);
	color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	color = ChromaAnimationAPI::LerpColor(color1, color2, t);
	SetKeyColor(colorsKeyboard, (int)Keyboard::RZKEY_V, color);

	/*
	// SHow health animation
	{
		int keys[] = {
			Keyboard::RZKEY::RZKEY_F1,
			Keyboard::RZKEY::RZKEY_F2,
			Keyboard::RZKEY::RZKEY_F3,
			Keyboard::RZKEY::RZKEY_F4,
			Keyboard::RZKEY::RZKEY_F5,
			Keyboard::RZKEY::RZKEY_F6,
		};
		int keysLength = sizeof(keys) / sizeof(int);

		float t = ms * 0.002f;
		float hp = fabsf(cos(MATH_PI / 2.0f + t));
		for (int i = 0; i < keysLength; ++i) {
			float ratio = (i + 1) / (float)keysLength;
			int color = ChromaAnimationAPI::GetRGB(0, (int)(255 * (1 - hp)), 0);
			if (((i + 1) / ((float)keysLength + 1)) < hp) {
				color = ChromaAnimationAPI::GetRGB(0, 255, 0);
			}
			else {
				color = ChromaAnimationAPI::GetRGB(0, 100, 0);
			}
			int key = keys[i];
			SetKeyColor(colorsKeyboard, key, color);
		}
	}

	// Show ammo animation
	{
		int keys[] = {
			Keyboard::RZKEY::RZKEY_F7,
			Keyboard::RZKEY::RZKEY_F8,
			Keyboard::RZKEY::RZKEY_F9,
			Keyboard::RZKEY::RZKEY_F10,
			Keyboard::RZKEY::RZKEY_F11,
			Keyboard::RZKEY::RZKEY_F12,
		};
		int keysLength = sizeof(keys) / sizeof(int);

		float t = ms * 0.001f;
		float hp = fabsf(cos(MATH_PI / 2.0f + t));
		for (int i = 0; i < keysLength; ++i) {
			float ratio = (i + 1) / (float)keysLength;
			int color = ChromaAnimationAPI::GetRGB((int)(255 * (1 - hp)), (int)(255 * (1 - hp)), 0);
			if (((i + 1) / ((float)keysLength + 1)) < hp) {
				color = ChromaAnimationAPI::GetRGB(255, 255, 0);
			}
			else {
				color = ChromaAnimationAPI::GetRGB(100, 100, 0);
			}
			int key = keys[i];
			SetKeyColor(colorsKeyboard, key, color);
		}
	}
	*/
}

void SetAmbientColor(int ambientColor,
	int* colorsChromaLink,
	int* colorsHeadset,
	int* colorsKeyboard,
	int* colorsKeypad,
	int* colorsMouse,
	int* colorsMousepad)
{
	// Set ambient color
	for (int d = (int)EChromaSDKDeviceEnum::DE_ChromaLink; d < (int)EChromaSDKDeviceEnum::DE_MAX; ++d)
	{
		switch ((EChromaSDKDeviceEnum)d)
		{
		case EChromaSDKDeviceEnum::DE_ChromaLink:
			SetAmbientColor1D(EChromaSDKDevice1DEnum::DE_ChromaLink, colorsChromaLink, ambientColor);
			break;
		case EChromaSDKDeviceEnum::DE_Headset:
			SetAmbientColor1D(EChromaSDKDevice1DEnum::DE_Headset, colorsHeadset, ambientColor);
			break;
		case EChromaSDKDeviceEnum::DE_Keyboard:
			SetAmbientColor2D(EChromaSDKDevice2DEnum::DE_Keyboard, colorsKeyboard, ambientColor);
			break;
		case EChromaSDKDeviceEnum::DE_Keypad:
			SetAmbientColor2D(EChromaSDKDevice2DEnum::DE_Keypad, colorsKeypad, ambientColor);
			break;
		case EChromaSDKDeviceEnum::DE_Mouse:
			SetAmbientColor2D(EChromaSDKDevice2DEnum::DE_Mouse, colorsMouse, ambientColor);
			break;
		case EChromaSDKDeviceEnum::DE_Mousepad:
			SetAmbientColor1D(EChromaSDKDevice1DEnum::DE_Mousepad, colorsMousepad, ambientColor);
			break;
		}
	}
}

void GameLoop()
{
	const int sizeChromaLink = GetColorArraySize1D(EChromaSDKDevice1DEnum::DE_ChromaLink);
	const int sizeHeadset = GetColorArraySize1D(EChromaSDKDevice1DEnum::DE_Headset);
	const int sizeKeyboard = GetColorArraySize2D(EChromaSDKDevice2DEnum::DE_Keyboard);
	const int sizeKeypad = GetColorArraySize2D(EChromaSDKDevice2DEnum::DE_Keypad);
	const int sizeMouse = GetColorArraySize2D(EChromaSDKDevice2DEnum::DE_Mouse);
	const int sizeMousepad = GetColorArraySize1D(EChromaSDKDevice1DEnum::DE_Mousepad);

	int* colorsChromaLink = new int[sizeChromaLink];
	int* colorsHeadset = new int[sizeHeadset];
	int* colorsKeyboard = new int[sizeKeyboard];
	int* colorsKeypad = new int[sizeKeypad];
	int* colorsMouse = new int[sizeMouse];
	int* colorsMousepad = new int[sizeMousepad];

	int* tempColorsChromaLink = new int[sizeChromaLink];	
	int* tempColorsHeadset = new int[sizeHeadset];	
	int* tempColorsKeyboard = new int[sizeKeyboard];	
	int* tempColorsKeypad = new int[sizeKeypad];	
	int* tempColorsMouse = new int[sizeMouse];	
	int* tempColorsMousepad = new int[sizeMousepad];

	int animationIdChromaLink = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_CHROMA_LINK);
	int animationIdHeadset = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_HEADSET);
	int animationIdKeyboard = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_KEYBOARD);
	int animationIdKeypad = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_KEYPAD);
	int animationIdMouse = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_MOUSE);
	int animationIdMousepad = ChromaAnimationAPI::GetAnimation(ANIMATION_FINAL_MOUSEPAD);

	int ambientColor = ChromaAnimationAPI::GetRGB(25, 37, 50);

	while (_sWaitForExit)
	{
		// start with a blank frame
		memset(colorsChromaLink, 0, sizeof(int) * sizeChromaLink);
		memset(colorsHeadset, 0, sizeof(int) * sizeHeadset);
		memset(colorsKeyboard, 0, sizeof(int) * sizeKeyboard);
		memset(colorsKeypad, 0, sizeof(int) * sizeKeypad);
		memset(colorsMouse, 0, sizeof(int) * sizeMouse);
		memset(colorsMousepad, 0, sizeof(int) * sizeMousepad);
		
		BlendAnimations(colorsChromaLink, tempColorsChromaLink,
			colorsHeadset, tempColorsHeadset,
			colorsKeyboard, tempColorsKeyboard,
			colorsKeypad, tempColorsKeypad,
			colorsMouse, tempColorsMouse,
			colorsMousepad, tempColorsMousepad);

		SetupKeyboardHotkeys(colorsKeyboard);		

		SetAmbientColor(ambientColor,
			colorsChromaLink,
			colorsHeadset,
			colorsKeyboard,
			colorsKeypad,
			colorsMouse,
			colorsMousepad);

		ChromaAnimationAPI::UpdateFrame(animationIdChromaLink, 0, 0.033f, colorsChromaLink, sizeChromaLink);
		ChromaAnimationAPI::UpdateFrame(animationIdHeadset, 0, 0.033f, colorsHeadset, sizeHeadset);
		ChromaAnimationAPI::UpdateFrame(animationIdKeyboard, 0, 0.033f, colorsKeyboard, sizeKeyboard);
		ChromaAnimationAPI::UpdateFrame(animationIdKeypad, 0, 0.033f, colorsKeypad, sizeKeypad);
		ChromaAnimationAPI::UpdateFrame(animationIdMouse, 0, 0.033f, colorsMouse, sizeMouse);
		ChromaAnimationAPI::UpdateFrame(animationIdMousepad, 0, 0.033f, colorsMousepad, sizeMousepad);

		// display the change
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_CHROMA_LINK, 0);
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_HEADSET, 0);
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_KEYBOARD, 0);
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_KEYPAD, 0);
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_MOUSE, 0);
		ChromaAnimationAPI::PreviewFrameName(ANIMATION_FINAL_MOUSEPAD, 0);

		//Sleep(33); //30 FPS
		Sleep(100); //10 FPS
	}
	delete[] colorsChromaLink;
	delete[] colorsHeadset;
	delete[] colorsKeyboard;
	delete[] colorsKeypad;
	delete[] colorsMouse;
	delete[] colorsMousepad;

	delete[] tempColorsChromaLink;
	delete[] tempColorsHeadset;
	delete[] tempColorsKeyboard;
	delete[] tempColorsKeypad;
	delete[] tempColorsMouse;
	delete[] tempColorsMousepad;
}

void HandleInputHost()
{
	lock_guard<mutex> guard(_sPlayersMutex);
	system("CLS");
	cout << "Type in SERVER HOST and press enter: ";
	string val;
	cin >> val;
	if (!val.empty())
	{
		_sServerHost = val;
		WriteConfig();
	}
	PrintLegend();
}

void HandleInputPort()
{
	lock_guard<mutex> guard(_sPlayersMutex);
	system("CLS");
	cout << "Type in SERVER PORT and press enter: ";
	string val;
	cin >> val;
	if (!val.empty())
	{
		_sServerPort = val;
		WriteConfig();
	}
	PrintLegend();
}

void HandleInputPlayer()
{
	lock_guard<mutex> guard(_sPlayersMutex);
	system("CLS");
	cout << "Type in PLAYER and press enter: ";
	string val;
	cin >> val;
	if (!val.empty())
	{
		_sSelectedPlayer = val;
		WriteConfig();
	}
	PrintLegend();
}

void HandleInputSelectPlayer()
{
	lock_guard<mutex> guard(_sPlayersMutex);
	if (_sPlayers.size() > 0)
	{
		_sSelectedPlayer = _sPlayers[(GetSelectedPlayerIndex() + 1) % _sPlayers.size()];
		WriteConfig();
		PrintLegend();
	}
}

int GetSelectedPlayerIndex()
{
	for (unsigned int i = 0; i < _sPlayers.size(); ++i)
	{
		string player = _sPlayers[i];
		if (!_stricmp(player.c_str(), _sSelectedPlayer.c_str()))
		{
			return i;
		}
	}
	return -1;
}

void HandleInput()
{
	while (_sWaitForExit)
	{
		int input = _getch();
		switch (input)
		{
		case 27:
			_sWaitForExit = false;
			cout << "Exiting..." << endl;
			break;
		case 'H':
		case 'h':
			HandleInputHost();
			break;
		case 'P':
		case 'p':
			HandleInputPort();
			break;
		case 'E':
		case 'e':
			HandleInputPlayer();
			break;
		case 's':
		case 'S':
			HandleInputSelectPlayer();
			break;
		}
		Sleep(0);
	}
}

void Cleanup()
{
	ChromaAnimationAPI::StopAll();
	ChromaAnimationAPI::CloseAll();
	RZRESULT result = ChromaAnimationAPI::Uninit();
	if (result != RZRESULT_SUCCESS)
	{
		cerr << "Failed to uninitialize Chroma!" << endl;
		exit(1);
	}
}

void PrintLegend()
{
	system("CLS");
	cout << "C++ RUST CHROMA MOD CLIENT" << endl;
	cout << "HOST: " << _sServerHost << endl;
	cout << "PORT: " << _sServerPort << endl;
	cout << "PLAYER: " << _sSelectedPlayer << endl;
	cout << endl;
	cout << "Press `ESC` to quit." << endl;
	cout << "Press `H` to change host." << endl;
	cout << "Press `P` to change port." << endl;
	cout << "Press `S` to select player." << endl;
	cout << "Press `E` to enter player name." << endl;
	cout << endl;
	cout << "PLAYERS:" << endl;
	for (unsigned int i = 0; i < _sPlayers.size(); ++i)
	{
		string player = _sPlayers[i];
		if (!strcmp(player.c_str(), _sSelectedPlayer.c_str()))
		{
			cout << "  <" << player << ">   ";
		}
		else
		{
			cout << "   " << player << "    ";
		}
	}
	cout << endl;
	if (!_sWaitForExit)
	{
		cout << "Exiting..." << endl;
	}
}

int main()
{
	ReadConfig();

	thread threadPlayers(GetServerPlayers);
	thread threadPlayer(GetServerPlayer);

	Init();
	SetupAnimations();
	thread thread(GameLoop);
	PrintLegend();
	HandleInput();
	thread.join();
	threadPlayers.join();
	threadPlayer.join();
	Cleanup();
	return 0;
}


#ifdef _DEBUG
#pragma comment( lib, "msvcrtd" )
#else
#pragma comment( lib, "msvcrt" )
#endif
#pragma comment( lib, "Wldap32" )
#pragma comment( lib, "Ws2_32" )
#pragma comment (lib, "crypt32")
#pragma comment( lib, "libcurl" )
#pragma comment( lib, "libssh2" )
#pragma comment( lib, "libcrypto" )
#pragma comment( lib, "libssl" )
#pragma comment( lib, "zlibstat" )
#pragma comment (lib, "jsoncpp")
