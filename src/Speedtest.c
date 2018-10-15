/*
	Main program.

	Michał Obrembski (byku@byku.com.pl)
*/
#include "csv.h"
#include "http.h"
#include "SpeedtestConfig.h"
#include "SpeedtestServers.h"
#include "Speedtest.h"
#include "SpeedtestLatencyTest.h"
#include "SpeedtestDownloadTest.h"
#include "SpeedtestUploadTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>

// strdup isnt a C99 function, so we need it to define itself
char *strdup(const char *str)
{
    int n = strlen(str) + 1;
    char *dup = (char*)malloc(n);
    if(dup)
    {
        strcpy(dup, str);
    }
    return dup;
}

int sortServers(SPEEDTESTSERVER_T **srv1, SPEEDTESTSERVER_T **srv2)
{
    return((*srv1)->distance - (*srv2)->distance);
}

float getElapsedTime(struct timeval tval_start) {
    struct timeval tval_end, tval_diff;
    gettimeofday(&tval_end, NULL);
    tval_diff.tv_sec = tval_end.tv_sec - tval_start.tv_sec;
    tval_diff.tv_usec = tval_end.tv_usec - tval_start.tv_usec;
    if(tval_diff.tv_usec < 0) {
        --tval_diff.tv_sec;
        tval_diff.tv_usec += 1000000;
    }
    return (float)tval_diff.tv_sec + (float)tval_diff.tv_usec / 1000000;
}

void parseCmdLine(int argc, char **argv) {
    int i;
    for(i=1; i<argc; i++)
    {
        if(strcmp("--help", argv[i]) == 0 || strcmp("-h", argv[i]) == 0)
        {
            printf("Usage (options are case sensitive):\n\
            \t--help - Show this help.\n\
            \t--server URL - use server URL, don'read config.\n\
            \t--upsize SIZE - use upload size of SIZE bytes.\n\
            \t--downtimes TIMES - how many times repeat download test.\n\
            \tSingle download test is downloading 30MB file.\n\
            \t--randomize NUMBER - randomize server usage for NUMBER of best servers\n\
            \t--csv - Output the results in csv in the format:\n\
            \tdate (ISO 8601 format), ping (ms), download (Mbit/s),upload (Mbit/s)\n\
            \nDefault action: Get server from Speedtest.NET infrastructure\n\
            and test download with 30MB download size and 1MB upload size.\n");
            exit(1);
        }
        if(strcmp("--server", argv[i]) == 0)
        {
            downloadUrl = (char*)malloc(sizeof(char) * strlen(argv[i+1]) + 1);
            strcpy(downloadUrl, argv[i + 1]);
        }
        if(strcmp("--upsize", argv[i]) == 0)
        {
            totalToBeTransfered = strtoul(argv[i + 1], NULL, 10);
        }
        if(strcmp("--downtimes", argv[i]) == 0)
        {
            totalDownloadTestCount = strtoul(argv[i + 1], NULL, 10);
        }
        if(strcmp("--randomize", argv[i]) == 0)
        {
            randomizeBestServers = strtoul(argv[i + 1], NULL, 10);
        }
        if(strcmp("--csv", argv[i]) == 0)
        {
            csvOutput=1;
        }
    }
}

void freeMem()
{
    free(latencyUrl);
    free(downloadUrl);
    free(uploadUrl);
    free(serverList);
    free(speedTestConfig);
}

void getBestServer()
{
    size_t selectedServer = 0;
    speedTestConfig = getConfig();
    if (speedTestConfig == NULL)
    {
        if(!csvOutput) printf("Cannot download speedtest.net configuration. Something is wrong...\n");
        freeMem();
        exit(1);
    }
    if(!csvOutput) printf("Your IP: %s And ISP: %s\n",
                speedTestConfig->ip, speedTestConfig->isp);
    if(!csvOutput) printf("Lat: %f Lon: %f\n", speedTestConfig->lat, speedTestConfig->lon);
    serverList = getServers(&serverCount, "http://www.speedtest.net/speedtest-servers-static.php");
    if (serverCount == 0)
    {
        // Primary server is not responding. Let's give a try with secondary one.
        serverList = getServers(&serverCount, "http://c.speedtest.net/speedtest-servers-static.php");
    }
    if(!csvOutput) printf("Grabbed %d servers\n", serverCount);
    if (serverCount == 0)
    {
        if(!csvOutput) printf("Cannot download any speedtest.net server. Something is wrong...\n");
        freeMem();
        exit(1);
    }
    for(i=0; i<serverCount; i++)
        serverList[i]->distance = haversineDistance(speedTestConfig->lat,
            speedTestConfig->lon,
            serverList[i]->lat,
            serverList[i]->lon);

    qsort(serverList, serverCount, sizeof(SPEEDTESTSERVER_T *),
                (int (*)(const void *,const void *)) sortServers);

    if (randomizeBestServers != 0) {
        if(!csvOutput) printf("Randomizing selection of %d best servers...\n", randomizeBestServers);
        srand(time(NULL));
        selectedServer = rand() % randomizeBestServers;
    }

    if(!csvOutput) printf("Best Server URL: %s\n\t Name: %s Country: %s Sponsor: %s Dist: %ld km\n",
        serverList[selectedServer]->url, serverList[selectedServer]->name, serverList[selectedServer]->country,
        serverList[selectedServer]->sponsor, serverList[selectedServer]->distance);
    downloadUrl = getServerDownloadUrl(serverList[selectedServer]->url);
    uploadUrl = (char*)malloc(sizeof(char) * strlen(serverList[selectedServer]->url) + 1);
    strcpy(uploadUrl, serverList[selectedServer]->url);

    for(i=0; i<serverCount; i++){
        free(serverList[i]->url);
        free(serverList[i]->name);
        free(serverList[i]->sponsor);
        free(serverList[i]->country);
        free(serverList[i]);
    }
}

static void getUserDefinedServer()
{
    /* When user specify server URL, then we're not downloading config,
    so we need to specify thread count */
    speedTestConfig = (struct speedtestConfig*)malloc(sizeof(struct speedtestConfig));
    speedTestConfig->downloadThreadConfig.threadsCount = 4;
    speedTestConfig->uploadThreadConfig.threadsCount = 2;
    speedTestConfig->uploadThreadConfig.length = 3;

    uploadUrl = downloadUrl;
    tmpUrl = (char*)malloc(sizeof(char) * strlen(downloadUrl) + 1);
    strcpy(tmpUrl, downloadUrl);
    downloadUrl = getServerDownloadUrl(tmpUrl);
    free(tmpUrl);
}

int main(int argc, char **argv)
{
  csvOutput=0;
  totalTransfered = 1024 * 1024;
  totalToBeTransfered = 1024 * 1024;
  totalDownloadTestCount = 1;
  randomizeBestServers = 0;
  speedTestConfig = NULL;
  parseCmdLine(argc, argv);

  if(downloadUrl == NULL)
  {
      getBestServer();
  }
  else
  {
      getUserDefinedServer();
  }

  latencyUrl = getLatencyUrl(uploadUrl);
  if(csvOutput)
  {
      time_t rawtime;
      struct tm * timeinfo;

      time (&rawtime);
      timeinfo = localtime (&rawtime);
      printf("%04d-%02d-%02dT%02d:%02d:%02dZ,",
            timeinfo->tm_year+1900,
            timeinfo->tm_mon+1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);
  }
  testLatency(latencyUrl);
  testDownload(downloadUrl);
  testUpload(uploadUrl);

  freeMem();
  return 0;
}
