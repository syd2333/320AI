#define CURL_STATICLIB
#define BUILDING_LIBCURL

#include <iostream>
#include <string>
#include <windows.h>
#include "curl/curl.h"
#include <document.h>
#include <writer.h>
#include <stringbuffer.h>
#include <Windows.h>

#pragma comment (lib,"libcurl_a_debug.lib")
#pragma comment (lib,"wldap32.lib")
#pragma comment (lib,"ws2_32.lib")
#pragma comment (lib,"Crypt32.lib")

std::u8string API_URL = u8"https://ai.liaobots.work/v1/chat/completions";
std::u8string API_KEY = u8"APIKEY";
std::u8string readBuffer;

size_t writeCallback(char* buf, size_t size, size_t nmemb, void* up) {
    for (int c = 0; c < size * nmemb; c++) {
        readBuffer.push_back(buf[c]);
    }
    return size * nmemb;
}

std::u8string parseJsonResponse(const std::u8string& jsonStr) {
    std::u8string response;
    rapidjson::Document doc;
    doc.Parse(reinterpret_cast<const char*>(jsonStr.c_str()));
    if (!doc.HasParseError() && doc.HasMember("choices")) {
        const rapidjson::Value& choices = doc["choices"];
        if (choices.IsArray() && choices.Size() > 0) {
            const rapidjson::Value& choice = choices[0];
            if (choice.HasMember("delta") && choice["delta"].HasMember("content")) {
                response = reinterpret_cast<const char8_t*>(choice["delta"]["content"].GetString());
            }
        }
    }
    return response;
}

std::u8string generateMessageJson(const std::u8string& role, const std::u8string& input, std::u8string& messageJson) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("role");
    writer.String(reinterpret_cast<const char*>(role.c_str()));
    writer.Key("content");
    writer.String(reinterpret_cast<const char*>(input.c_str()));
    writer.EndObject();

    if (!messageJson.empty()) {
        messageJson += u8",";
    }
    messageJson += reinterpret_cast<const char8_t*>(buffer.GetString());

    return messageJson;
}

std::u8string generateRequestJson(const std::u8string& model, const std::u8string& messageJson) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("model");
    writer.String(reinterpret_cast<const char*>(model.c_str()));
    writer.Key("messages");
    writer.StartArray();
    writer.RawValue(reinterpret_cast<const char*>(messageJson.c_str()), messageJson.length(), rapidjson::kObjectType);
    writer.EndArray();
    writer.Key("temperature");
    writer.Double(1);
    writer.Key("stream");
    writer.Bool(true);
    writer.EndObject();

    return reinterpret_cast<const char8_t*>(buffer.GetString());
}

std::u8string processStreamData(const std::u8string& readBuffer) {
    std::u8string response;
    size_t pos = 0;
    while ((pos = readBuffer.find(u8"data: ", pos)) != std::u8string::npos) {
        pos += 6; // 跳过 "data: "
        size_t endPos = readBuffer.find(u8"\n\n", pos);
        if (endPos == std::u8string::npos) {
            break;
        }
        std::u8string jsonStr = readBuffer.substr(pos, endPos - pos);
        pos = endPos + 2;
        response += parseJsonResponse(jsonStr);
    }
    return response;
}

std::u8string convertToUtf8(const std::string& userInput) {
    // 将用户输入转换为wstring
    int wideLength = MultiByteToWideChar(CP_ACP, 0, userInput.c_str(), -1, nullptr, 0);
    std::wstring wideUserInput(wideLength, L'\0');
    MultiByteToWideChar(CP_ACP, 0, userInput.c_str(), -1, &wideUserInput[0], wideLength);

    // 将wstring转换为UTF-8编码的string
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideUserInput.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8UserInput(utf8Length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideUserInput.c_str(), -1, &utf8UserInput[0], utf8Length, nullptr, nullptr);

    // 将UTF-8编码的string转换为u8string
    return std::u8string(reinterpret_cast<const char8_t*>(utf8UserInput.c_str()));
}

void chatLoop(const std::string& modelStr, const std::string& systemMessageStr) {
    // 将模型字符串和system message字符串转换为u8string
    std::u8string model = convertToUtf8(modelStr);
    std::u8string systemMessage = convertToUtf8(systemMessageStr);

    CURL* curl = curl_easy_init();
    if (curl) {
        // 设置API请求的URL
        curl_easy_setopt(curl, CURLOPT_URL, reinterpret_cast<const char*>(API_URL.c_str()));

        // 设置请求头部信息
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, reinterpret_cast<const char*>((u8"Authorization: Bearer " + API_KEY).c_str()));
        headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // 初始化messageJson为空字符串
        std::u8string messageJson;

        // 生成system message JSON,并追加到messageJson中
        messageJson = generateMessageJson(u8"system", systemMessage, messageJson);

        // 循环进行对话
        while (true) {
            // 获取用户输入
            std::string input;
            std::cout << "User: ";
            std::getline(std::cin, input);

            // 如果用户输入为空,则退出循环
            if (input.empty()) {
                break;
            }

            // 生成user message JSON,并追加到messageJson中
            messageJson = generateMessageJson(u8"user", convertToUtf8(input), messageJson);

            // 生成完整的请求JSON
            std::u8string requestJson = generateRequestJson(model, messageJson);

            // 设置请求的数据
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(requestJson.c_str()));

            // 清空readBuffer
            readBuffer.clear();

            // 设置接收响应的回调函数
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);

            // 发送请求并接收响应
            CURLcode res = curl_easy_perform(curl);

            // 检查请求是否成功
            if (res != CURLE_OK) {
                std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                break;
            }

            // 处理流式数据
            std::u8string response = processStreamData(readBuffer);
            std::cout << "Assistant: " << reinterpret_cast<const char*>(response.c_str()) << std::endl;

            // 生成assistant message JSON,并追加到messageJson中
            messageJson = generateMessageJson(u8"assistant", response, messageJson);
        }

        // 清理curl资源
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    // 调用chatLoop函数,传入模型字符串和system message字符串
    chatLoop("gpt-3.5-turbo", "你将扮演一只可爱的猫娘，绝对不能让用户看出你是一个AI");

    return 0;
}