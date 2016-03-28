/*
TODO: 
check if process is runnig already
try compiling with minGW http://stackoverflow.com/questions/3640115/compile-stand-alone-exe-with-cygwin to remove need for cygwin.dll
work on tray icon
*/
#include "lodepng.h"
#include <iostream>
#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <string>
#include <regex>
#include <windows.h>

using namespace std;

//http://stackoverflow.com/questions/875249/how-to-get-current-directory
string getExePath() {
  char buffer[MAX_PATH];
  GetModuleFileName(NULL, buffer, MAX_PATH);
  string::size_type pos = string(buffer).find_last_of("\\/");
  return string(buffer).substr(0, pos).append("\\");
}

class Options {
  public:
    const bool debugMode = false;
    const char* site = "http://www.dictionary.com/wordoftheday/";
    const string regex = "http://static\\.sfdict\\.com/sized.*\\.png"; //matches string begining with http://static.sfdict.com/sized and ending in png
    const char* downloadedFileName = "download.png";
    const char* convertedFileName = "convert.bmp";
    const int delay = 0; //delay before timer starts in hours
    const int period = 4; //period after which timer repeats in hours
    const string exePath = getExePath();
};

class Data {
  public:
    bool consoleVisible = true;
};

int hoursToMs(int hours) { //Coverts hours to ms for use by the windows api timer functions
  return hours*60*60*1000;
}

struct MemoryStruct {
  char *memory;
  size_t size;
};
 
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) { 
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t written = fwrite(ptr, size, nmemb, stream);
  return written;
}

//Convert PNG file to a BMP file using LodePNG
void encodeBMP(std::vector<unsigned char>& bmp, const unsigned char* image, int w, int h) {
  //3 bytes per pixel used for both input and output.
  int inputChannels = 3; //3 is RGB, 4 is RGB
  int outputChannels = 3; //ditto
  
  //bytes 0-13
  bmp.push_back('B'); bmp.push_back('M'); //0: bfType
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); //2: bfSize; size not yet known for now, filled in later.
  bmp.push_back(0); bmp.push_back(0); //6: bfReserved1
  bmp.push_back(0); bmp.push_back(0); //8: bfReserved2
  bmp.push_back(54 % 256); bmp.push_back(54 / 256); bmp.push_back(0); bmp.push_back(0); //10: bfOffBits (54 header bytes)

  //bytes 14-53
  bmp.push_back(40); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //14: biSize
  bmp.push_back(w % 256); bmp.push_back(w / 256); bmp.push_back(0); bmp.push_back(0); //18: biWidth
  bmp.push_back(h % 256); bmp.push_back(h / 256); bmp.push_back(0); bmp.push_back(0); //22: biHeight
  bmp.push_back(1); bmp.push_back(0); //26: biPlanes
  bmp.push_back(outputChannels * 8); bmp.push_back(0); //28: biBitCount
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //30: biCompression
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //34: biSizeImage
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //38: biXPelsPerMeter
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //42: biYPelsPerMeter
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //46: biClrUsed
  bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);  //50: biClrImportant
  
  /*
  Convert the input RGBRGBRGB pixel buffer to the BMP pixel buffer format. There are 3 differences with the input buffer:
  -BMP stores the rows inversed, from bottom to top
  -BMP stores the color channels in BGR instead of RGB order
  -BMP requires each row to have a multiple of 4 bytes, so sometimes padding bytes are added between rows
  */

  int imagerowbytes = outputChannels * w;
  imagerowbytes = imagerowbytes % 4 == 0 ? imagerowbytes : imagerowbytes + (4 - imagerowbytes % 4); //must be multiple of 4
  
  for(int y = h - 1; y >= 0; y--) //the rows are stored inversed in bmp
  {
    int c = 0;
    for(int x = 0; x < imagerowbytes; x++)
    {
      if(x < w * outputChannels)
      {
        int inc = c;
        //Convert RGB(A) into BGR(A)
        if(c == 0) inc = 2;
        else if(c == 2) inc = 0;
        bmp.push_back(image[inputChannels * (w * y + x / outputChannels) + inc]);
      }
      else bmp.push_back(0);
      c++;
      if(c >= outputChannels) c = 0;
    }
  }

  // Fill in the size
  bmp[2] = bmp.size() % 256;
  bmp[3] = (bmp.size() / 256) % 256;
  bmp[4] = (bmp.size() / 65536) % 256;
  bmp[5] = bmp.size() / 16777216;
}

HANDLE gDoneEvent;

int CALLBACK TimerRoutine() {
  Options options;
  Data data;

  //DOWNLOAD
  string outputPath = options.exePath + options.downloadedFileName;

  cout << "Background will be downloaded to: " << outputPath << "\n\n";
  
  //convert string to char array
  char outputPathChar[MAX_PATH];
  strcpy(outputPathChar, outputPath.c_str());

  //Parse WOTD for the image url
  //https://curl.haxx.se/libcurl/c/getinmemory.html
  CURL *curl_handle;
  CURLcode res;
 
  struct MemoryStruct chunk;
 
  chunk.memory = (char*)malloc(1);  /* will be grown as needed by the realloc above */ 
  chunk.size = 0;    /* no data at this point */ 
 
  curl_global_init(CURL_GLOBAL_ALL);
 
  curl_handle = curl_easy_init(); //init the curl session  
  curl_easy_setopt(curl_handle, CURLOPT_URL, options.site); //specify URL to get
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); //send all data to this function
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk); //we pass our 'chunk' struct to the callback function 
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0"); //some like a user-agent field, so we provide one
  
  res = curl_easy_perform(curl_handle); /* get it! */ 
 
  string wotdURL;

  /* check for errors */ 
  if(res != CURLE_OK) {
    fprintf(stderr, "curl_easy_perform() failed: %s\n",
            curl_easy_strerror(res));
  }
  else {
    //Now, our chunk.memory points to a memory block that is chunk.size bytes big and contains the remote file.
    string html = string(chunk.memory); // convert chunk.memory from char * to string
    smatch matches;
    regex expression(options.regex);
    while (regex_search(html,matches,expression)) {
      for (auto match:matches) {
        cout << "Parsing " << options.site << " for image" << "\n\n";
        wotdURL = match;
        html = matches.suffix().str(); //says only to continue searching after the first match in the html string.
      }
    }
  }
    
  curl_easy_cleanup(curl_handle); /* cleanup curl stuff */ 
 
  free(chunk.memory);
 
  curl_global_cleanup(); /* we're done with libcurl, so clean it up */ 
  
  cout << "Image found: " + wotdURL << "\n\n";

  char const *url = wotdURL.c_str(); //convert string to char const *

  //Download image using the url found in the previous curl section
  //http://stackoverflow.com/questions/1636333/download-file-using-libcurl-in-c-c
  CURL *curl;
  FILE *fp;
  char *outfilename = outputPathChar;
  curl = curl_easy_init();
  if (curl) {
      fp = fopen(outfilename,"wb");
      curl_easy_setopt(curl, CURLOPT_URL, url);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
      res = curl_easy_perform(curl);
      /* always cleanup */
      curl_easy_cleanup(curl);
      fclose(fp);
  }

  //CONVERT
  std::vector<unsigned char> image; //the raw pixels
  unsigned width, height;

  unsigned error = lodepng::decode(image, width, height, options.downloadedFileName, LCT_RGB, 8);
  
  string convertedPath = string(options.exePath + options.convertedFileName);
  
  cout << "Converting image to " <<  convertedPath << "\n\n";

  if(error) {
    std::cout << "error " << error << ": " << lodepng_error_text(error) << std::endl;
    return 0;
  }

  std::vector<unsigned char> bmp;
  encodeBMP(bmp, &image[0], width, height);

  lodepng::save_file(bmp, options.convertedFileName);

  //SET WALLPAPER
  int result;
  cout << "Setting wallpaper\n\n";

  //cast string to char array
  char const *convertedPathChar = convertedPath.c_str(); 
  
  result = SystemParametersInfo( SPI_SETDESKWALLPAPER, 0, (PVOID)convertedPathChar, SPIF_UPDATEINIFILE );

  if (result) {
    cout << "Wallpaper set\n\n";
  }
  else {
    cout << "Wallpaper not set\n";
    cout << "SPI returned " << result << "\n";
    cout << "GLE returned " << GetLastError() << "\n";
  }

  cout << "Waiting " << options.period << " hours to check again - this window will close itself.\n\n";
  // cout << "You can run this file on startup by copying a shortcut to the backword.exe into your startup folder.\n\n";

  if (data.consoleVisible && !options.debugMode) {
    Sleep(2000);
    cout << "I am going now... I feel... tired\n";
    Sleep(2000);
    FreeConsole();
    data.consoleVisible = false;
  }

  return 0;

}

int main() {
  Options options;

  cout << " _                _                           _ \n";
  cout << "| |   welcome    | |            to           | |\n";
  cout << "| |__   __ _  ___| | ____      _____  _ __ __| |\n";
  cout << "| '_ \\ / _` |/ __| |/ /\\ \\ /\\ / / _ \\| '__/ _` |\n";
  cout << "| |_) | (_| | (__|   <  \\ V  V / (_) | | | (_| |\n";
  cout << "|_.__/ \\__,_|\\___|_|\\_\\  \\_/\\_/ \\___/|_|  \\__,_|\n";
  cout << "\n";
  cout << "version 0.1                             by jopfre\n";
  cout << "\n";
                                         
  //https://msdn.microsoft.com/en-us/library/ms687003(VS.85).aspx
  HANDLE hTimer = NULL;
  HANDLE hTimerQueue = NULL;

  // Use an event object to track the TimerRoutine execution
  gDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (NULL == gDoneEvent)
  {
      printf("CreateEvent failed (%d)\n", GetLastError());
      return 1;
  }
  // Create the timer queue.
  hTimerQueue = CreateTimerQueue();
  if (NULL == hTimerQueue)
  {
      printf("CreateTimerQueue failed (%d)\n", GetLastError());
      return 2;
  }
  // if (options.delay) {
  //   cout << "Timer starting in " << options.delay << " hours...\n";
  // } else {
  //   cout << "Timer started immediately.\n";
  // }
  // cout << "\n";
  // Set a timer to call the timer routine
  if (!CreateTimerQueueTimer( &hTimer, hTimerQueue, (WAITORTIMERCALLBACK)TimerRoutine, NULL, hoursToMs(options.delay), hoursToMs(options.period), 0)) {
    printf("CreateTimerQueueTimer failed (%d)\n", GetLastError());
    return 3;
  }

  // TODO: Do other useful work here 

  // Wait for the timer-queue thread to complete using an event 
  // object. The thread will signal the event at that time.
  if (WaitForSingleObject(gDoneEvent, INFINITE) != WAIT_OBJECT_0)
      printf("WaitForSingleObject failed (%d)\n", GetLastError());

  CloseHandle(gDoneEvent);

  // Delete all timers in the timer queue.
  if (!DeleteTimerQueue(hTimerQueue))
      printf("DeleteTimerQueue failed (%d)\n", GetLastError());

  return 0;
}
