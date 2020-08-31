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
static unordered_map<unsigned int, int> _sFrameIndexes;

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

void QueueAnimation(unsigned int index)
{
	lock_guard<mutex> guard(_sPlayAnimationMutex);
	_sFrameIndexes[index] = 0; // start
}

void GetServerPlayer()
{
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
				//cout << "Response: player.json ===" << endl << response_string << endl;
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
								QueueAnimation(1);
							}
							else if (!strcmp(dataEvent.c_str(), "OnActiveItemChanged"))
							{
								QueueAnimation(2);
							}
							else if (!strcmp(dataEvent.c_str(), "OnMessagePlayer"))
							{
								if (!strcmp(dataMessage.c_str(), "Can't afford to place!"))
								{
									QueueAnimation(3);
								}
							}
							else if (!strcmp(dataEvent.c_str(), "OnMeleeThrown"))
							{
								QueueAnimation(4);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerJump"))
							{
								QueueAnimation(5);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerDuck"))
							{
								QueueAnimation(6);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerSprint"))
							{
								QueueAnimation(7);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerConnected"))
							{
								QueueAnimation(8);
							}
							else if (!strcmp(dataEvent.c_str(), "OnPlayerDeath"))
							{
								QueueAnimation(9);
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
void SetupEffect1Keyboard() {
	const char* baseLayer = "Animations/Effect1_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1ChromaLink() {
	const char* baseLayer = "Animations/Effect1_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Headset() {
	const char* baseLayer = "Animations/Effect1_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Mousepad() {
	const char* baseLayer = "Animations/Effect1_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Mouse() {
	const char* baseLayer = "Animations/Effect1_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect1Keypad() {
	const char* baseLayer = "Animations/Effect1_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 1);
}
void SetupEffect2Keyboard() {
	const char* baseLayer = "Animations/Effect2_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2ChromaLink() {
	const char* baseLayer = "Animations/Effect2_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Headset() {
	const char* baseLayer = "Animations/Effect2_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Mousepad() {
	const char* baseLayer = "Animations/Effect2_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Mouse() {
	const char* baseLayer = "Animations/Effect2_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect2Keypad() {
	const char* baseLayer = "Animations/Effect2_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 0, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 2);
}
void SetupEffect3Keyboard() {
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
	SetupEvent(baseLayer, 3);
}
void SetupEffect3ChromaLink() {
	const char* baseLayer = "Animations/Blank_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Headset() {
	const char* baseLayer = "Animations/Blank_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Mousepad() {
	const char* baseLayer = "Animations/Blank_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Mouse() {
	const char* baseLayer = "Animations/Blank_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect3Keypad() {
	const char* baseLayer = "Animations/Blank_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 0, 0);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 3);
}
void SetupEffect4Keyboard() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect4ChromaLink() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Headset() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Mousepad() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Mouse() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect4Keypad() {
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
	SetupEvent(baseLayer, 4);
}
void SetupEffect5Keyboard() {
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
	SetupEvent(baseLayer, 5);
}
void SetupEffect5ChromaLink() {
	const char* baseLayer = "Animations/Blank_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Headset() {
	const char* baseLayer = "Animations/Blank_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Mousepad() {
	const char* baseLayer = "Animations/Blank_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Mouse() {
	const char* baseLayer = "Animations/Blank_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect5Keypad() {
	const char* baseLayer = "Animations/Blank_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int frameCount = 40;
	ChromaAnimationAPI::MakeBlankFramesRGBName(baseLayer, frameCount, 0.033f, 255, 255, 255);
	ChromaAnimationAPI::FadeStartFramesName(baseLayer, 10);
	ChromaAnimationAPI::FadeEndFramesName(baseLayer, frameCount - 10);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 5);
}
void SetupEffect6Keyboard() {
	const char* baseLayer = "Animations/Effect6_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6ChromaLink() {
	const char* baseLayer = "Animations/Effect6_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Headset() {
	const char* baseLayer = "Animations/Effect6_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Mousepad() {
	const char* baseLayer = "Animations/Effect6_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Mouse() {
	const char* baseLayer = "Animations/Effect6_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect6Keypad() {
	const char* baseLayer = "Animations/Effect6_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 6);
}
void SetupEffect7Keyboard() {
	const char* baseLayer = "Animations/Effect7_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7ChromaLink() {
	const char* baseLayer = "Animations/Effect7_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Headset() {
	const char* baseLayer = "Animations/Effect7_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Mousepad() {
	const char* baseLayer = "Animations/Effect7_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Mouse() {
	const char* baseLayer = "Animations/Effect7_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect7Keypad() {
	const char* baseLayer = "Animations/Effect7_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(16, 16, 16);
	int color2 = ChromaAnimationAPI::GetRGB(255, 255, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 7);
}
void SetupEffect8Keyboard() {
	const char* baseLayer = "Animations/Effect8_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8ChromaLink() {
	const char* baseLayer = "Animations/Effect8_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Headset() {
	const char* baseLayer = "Animations/Effect8_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Mousepad() {
	const char* baseLayer = "Animations/Effect8_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Mouse() {
	const char* baseLayer = "Animations/Effect8_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect8Keypad() {
	const char* baseLayer = "Animations/Effect8_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(0, 255, 255);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 8);
}
void SetupEffect9Keyboard() {
	const char* baseLayer = "Animations/Effect9_Keyboard.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::SetChromaCustomFlagName(baseLayer, true);
	ChromaAnimationAPI::SetChromaCustomColorAllFramesName(baseLayer);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9ChromaLink() {
	const char* baseLayer = "Animations/Effect9_ChromaLink.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Headset() {
	const char* baseLayer = "Animations/Effect9_Headset.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Mousepad() {
	const char* baseLayer = "Animations/Effect9_Mousepad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Mouse() {
	const char* baseLayer = "Animations/Effect9_Mouse.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
}
void SetupEffect9Keypad() {
	const char* baseLayer = "Animations/Effect9_Keypad.chroma";
	ChromaAnimationAPI::CloseAnimationName(baseLayer);
	ChromaAnimationAPI::GetAnimation(baseLayer);
	int color1 = ChromaAnimationAPI::GetRGB(0, 127, 0);
	int color2 = ChromaAnimationAPI::GetRGB(127, 0, 0);
	ChromaAnimationAPI::MultiplyTargetColorLerpAllFramesName(baseLayer, color1, color2);
	ChromaAnimationAPI::OverrideFrameDurationName(baseLayer, 0.033f);
	SetupEvent(baseLayer, 9);
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
	SetupEffect1Mouse();
	SetupEffect1Mousepad();

	SetupEffect2ChromaLink();
	SetupEffect2Headset();
	SetupEffect2Keyboard();
	SetupEffect2Keypad();
	SetupEffect2Mouse();
	SetupEffect2Mousepad();

	SetupEffect3ChromaLink();
	SetupEffect3Headset();
	SetupEffect3Keyboard();
	SetupEffect3Keypad();
	SetupEffect3Mouse();
	SetupEffect3Mousepad();

	SetupEffect4ChromaLink();
	SetupEffect4Headset();
	SetupEffect4Keyboard();
	SetupEffect4Keypad();
	SetupEffect4Mouse();
	SetupEffect4Mousepad();

	SetupEffect5ChromaLink();
	SetupEffect5Headset();
	SetupEffect5Keyboard();
	SetupEffect5Keypad();
	SetupEffect5Mouse();
	SetupEffect5Mousepad();

	SetupEffect6ChromaLink();
	SetupEffect6Headset();
	SetupEffect6Keyboard();
	SetupEffect6Keypad();
	SetupEffect6Mouse();
	SetupEffect6Mousepad();

	SetupEffect7ChromaLink();
	SetupEffect7Headset();
	SetupEffect7Keyboard();
	SetupEffect7Keypad();
	SetupEffect7Mouse();
	SetupEffect7Mousepad();

	SetupEffect8ChromaLink();
	SetupEffect8Headset();
	SetupEffect8Keyboard();
	SetupEffect8Keypad();
	SetupEffect8Mouse();
	SetupEffect8Mousepad();

	SetupEffect9ChromaLink();
	SetupEffect9Headset();
	SetupEffect9Keyboard();
	SetupEffect9Keypad();
	SetupEffect9Mouse();
	SetupEffect9Mousepad();

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

void BlendAnimation1D(const pair<const unsigned int, int>& pair, EChromaSDKDevice1DEnum device, const char* animationName,
	int* colors, int* tempColors)
{
	const int size = GetColorArraySize1D(device);
	const int effectIndex = pair.first;
	const int frameId = pair.second;
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
		_sFrameIndexes[effectIndex] = frameId + 1;
	}
	else
	{
		_sFrameIndexes[effectIndex] = -1;
	}
}

void BlendAnimation2D(const pair<const unsigned int, int>& pair, EChromaSDKDevice2DEnum device, const char* animationName,
	int* colors, int* tempColors)
{
	const int size = GetColorArraySize2D(device);
	const int effectIndex = pair.first;
	const int frameId = pair.second;
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
		_sFrameIndexes[effectIndex] = frameId + 1;
	}
	else
	{
		_sFrameIndexes[effectIndex] = -1;
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
	for (pair<const unsigned int, int> pair : _sFrameIndexes)
	{
		if (pair.second >= 0)
		{
			//iterate all device types
			for (int d = (int)EChromaSDKDeviceEnum::DE_ChromaLink; d < (int)EChromaSDKDeviceEnum::DE_MAX; ++d)
			{
				string animationName = "Event\\Effect";
				animationName += to_string(pair.first);

				switch ((EChromaSDKDeviceEnum)d)
				{
				case EChromaSDKDeviceEnum::DE_ChromaLink:
					animationName += "_ChromaLink.chroma";
					BlendAnimation1D(pair, EChromaSDKDevice1DEnum::DE_ChromaLink, animationName.c_str(), colorsChromaLink, tempColorsChromaLink);
					break;
				case EChromaSDKDeviceEnum::DE_Headset:
					animationName += "_Headset.chroma";
					BlendAnimation1D(pair, EChromaSDKDevice1DEnum::DE_Headset, animationName.c_str(), colorsHeadset, tempColorsHeadset);
					break;
				case EChromaSDKDeviceEnum::DE_Keyboard:
					animationName += "_Keyboard.chroma";
					BlendAnimation2D(pair, EChromaSDKDevice2DEnum::DE_Keyboard, animationName.c_str(), colorsKeyboard, tempColorsKeyboard);
					break;
				case EChromaSDKDeviceEnum::DE_Keypad:
					animationName += "_Keypad.chroma";
					BlendAnimation2D(pair, EChromaSDKDevice2DEnum::DE_Keypad, animationName.c_str(), colorsKeypad, tempColorsKeypad);
					break;
				case EChromaSDKDeviceEnum::DE_Mouse:
					animationName += "_Mouse.chroma";
					BlendAnimation2D(pair, EChromaSDKDevice2DEnum::DE_Mouse, animationName.c_str(), colorsMouse, tempColorsMouse);
					break;
				case EChromaSDKDeviceEnum::DE_Mousepad:
					animationName += "_Mousepad.chroma";
					BlendAnimation1D(pair, EChromaSDKDevice1DEnum::DE_Mousepad, animationName.c_str(), colorsMousepad, tempColorsMousepad);
					break;
				}
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

		Sleep(33); //30 FPS
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
