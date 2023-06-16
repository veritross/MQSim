#include <iostream>
#include <cstring>
#include <string>
#include <cmath>


using namespace std;

#define MAX_BUFFER_SIZE 100

int main(){
    string inputFileName, outputFileName;
    cout << "type input file name / output file name" << endl;
    cin >> inputFileName >> outputFileName;
    FILE *inputFilePointer = fopen(inputFileName.c_str(), "r");
    FILE *outputFilePointer = fopen(outputFileName.c_str(), "w");
    char buffer[MAX_BUFFER_SIZE];
    char* deviceNumber;
    char* traceAction;
    char* operationType;
    char* timeStamp;
    double timeStampDouble;
    char* sectorAddress;
    char* requestSize;
    while(fgets(buffer, MAX_BUFFER_SIZE, inputFilePointer) != NULL){
        deviceNumber = strtok(buffer, " ");
        for(int i = 0; i < 2; i++){
            strtok(NULL, " ");
        }
        timeStamp = strtok(NULL, " ");
        strtok(NULL, " ");
        traceAction = strtok(NULL, " ");
        if(!strcmp(traceAction, "D")){
            char value[MAX_BUFFER_SIZE] = "";
            operationType = strtok(NULL, " ");
            sectorAddress = strtok(NULL, " ");
            strtok(NULL, " ");
            requestSize = strtok(NULL, " ");
            timeStampDouble = strtod(timeStamp, NULL);
            timeStampDouble *= pow(10, 9);
            sprintf(timeStamp, "%d", static_cast<int>(timeStampDouble));
            strcat(value, timeStamp);
            strcat(value, " ");
            strcat(value, deviceNumber);
            strcat(value, " ");
            strcat(value, sectorAddress);
            strcat(value, " ");
            strcat(value, requestSize);
            strcat(value, " ");
            strcat(value, (strcmp(operationType, "RS") ? "0" : "1"));
            strcat(value, "\n");
            fputs(value, outputFilePointer);
            
        }

    }

    fclose(inputFilePointer);
    fclose(outputFilePointer);

    return 0;
    
}