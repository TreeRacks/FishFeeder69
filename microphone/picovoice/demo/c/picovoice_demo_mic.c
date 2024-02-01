/*
    Copyright 2020-2021 Picovoice Inc.

    You may not use this file except in compliance with the license. A copy of the license is located in the "LICENSE"
    file accompanying this source.

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on
    an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
    specific language governing permissions and limitations under the License.
*/

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define yellowButtonPath "/sys/class/gpio/gpio27/value"

#define I2CDRV_LINUX_BUS0 "/dev/i2c-0"
#define I2CDRV_LINUX_BUS1 "/dev/i2c-1"
#define I2CDRV_LINUX_BUS2 "/dev/i2c-2"

#define numberOfMatrixRows 8
#define numberOfMatrixCols 8

#define I2C_DEVICE_ADDRESS 0x70
#define SYS_SETUP_REG 0X21
#define DISPLAY_SETUP_REG 0x81
#define EMPTY 0

static unsigned char logicalFrameArr[numberOfMatrixRows];
static unsigned char physicalFrameArr[numberOfMatrixRows*2];
static char* charRowByRowBits = 0;
static int charCurrentColumns = 0;

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

#else

#include <dlfcn.h>

#endif

#include "pv_picovoice.h"
#include "pv_recorder.h"


static pthread_t threadServo;
static pthread_t threadButton;
static bool stopServo;
static bool stopButton = false;
static bool servoTurnedOn = false;
static int mode = 0;

static volatile bool is_interrupted = false;

static void *open_dl(const char *dl_path) {

#if defined(_WIN32) || defined(_WIN64)

    return LoadLibrary(dl_path);

#else

    return dlopen(dl_path, RTLD_NOW);

#endif

}

static void *load_symbol(void *handle, const char *symbol) {

#if defined(_WIN32) || defined(_WIN64)

    return GetProcAddress((HMODULE) handle, symbol);

#else

    return dlsym(handle, symbol);

#endif

}

static void close_dl(void *handle) {

#if defined(_WIN32) || defined(_WIN64)

    FreeLibrary((HMODULE) handle);

#else

    dlclose(handle);

#endif

}

static void print_dl_error(const char *message) {

#if defined(_WIN32) || defined(_WIN64)

    fprintf(stderr, "%s with code '%lu'.\n", message, GetLastError());

#else

    fprintf(stderr, "%s with '%s'.\n", message, dlerror());

#endif

}

static struct option long_options[] = {
        {"show_audio_devices",    no_argument,       NULL, 'd'},
        {"library_path",          required_argument, NULL, 'l'},
        {"access_key",            required_argument, NULL, 'a'},
        {"keyword_path",          required_argument, NULL, 'k'},
        {"context_path",          required_argument, NULL, 'c'},
        {"porcupine_sensitivity", required_argument, NULL, 's'},
        {"porcupine_model_path",  required_argument, NULL, 'p'},
        {"rhino_sensitivity",     required_argument, NULL, 't'},
        {"rhino_model_path",      required_argument, NULL, 'r'},
        {"endpoint_duration_sec", required_argument, NULL, 'u'},
        {"require_endpoint",      required_argument, NULL, 'e'},
        {"audio_device_index",    required_argument, NULL, 'i'}
};

void print_usage(const char *program_name) {
    fprintf(stderr,
            "Usage : %s -l LIBRARY_PATH -a ACCESS_KEY -k KEYWORD_PATH -c CONTEXT_PATH -p PPN_MODEL_PATH -r RHN_MODEL_PATH "
            "[--audio_device_index AUDIO_DEVICE_INDEX --porcupine_sensitivity PPN_SENSITIVITY --rhino_sensitivity RHN_SENSITIVITY --endpoint_duration_sec --require_endpoint \"true\"|\"false\" ]\n"
            "       %s --show_audio_devices\n",
            program_name,
            program_name);
}

void interrupt_handler(int _) {
    (void) _;
    is_interrupted = true;
}

void show_audio_devices(void) {
    char **devices = NULL;
    int32_t count = 0;

    pv_recorder_status_t status = pv_recorder_get_audio_devices(&count, &devices);
    if (status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to get audio devices with: %s.\n", pv_recorder_status_to_string(status));
        exit(1);
    }

    fprintf(stdout, "Printing devices...\n");
    for (int32_t i = 0; i < count; i++) {
        fprintf(stdout, "index: %d, name: %s\n", i, devices[i]);
    }

    pv_recorder_free_device_list(count, devices);
}

static void wake_word_callback(void) {
    fprintf(stdout, "[wake word]\n");
    fflush(stdout);
}

static void (*pv_inference_delete_func)(pv_inference_t *) = NULL;

void runCommand(char* command)
{
    // Execute the shell command (output into pipe)
    FILE *pipe = popen(command, "r");
    // Ignore output of the command; but consume it
    // so we don't get an error when closing the pipe.
    char buffer[1024];
    while (!feof(pipe) && !ferror(pipe)) {
        if (fgets(buffer, sizeof(buffer), pipe) == NULL)
            break;
        // printf("--> %s", buffer); // Uncomment for debugging
        }
    // Get the exit code from the pipe; non-zero is an error:
    int exitCode = WEXITSTATUS(pclose(pipe));
    if (exitCode != 0) {
        perror("Unable to execute command:");
        printf(" command: %s\n", command);
        printf(" exit code: %d\n", exitCode);
    }
}

void sleepForMs(long long delayInMs){
    const long long NS_PER_MS = 1000 * 1000;
    const long long NS_PER_SECOND = 1000000000;
    long long delayNs = delayInMs * NS_PER_MS;
    int seconds = delayNs / NS_PER_SECOND;
    int nanoseconds = delayNs % NS_PER_SECOND;
    struct timespec reqDelay = {seconds, nanoseconds};
    nanosleep(&reqDelay, (struct timespec *) NULL);
}

static void writeTo(char* location, char* value)
{
    FILE* f = fopen(location, "w");
    fprintf(f, value);
    fclose(f);     
}

void defaultRelease(){
    writeTo("/sys/class/pwm/pwmchip3/pwm1/period", "20000000");
    writeTo("/sys/class/pwm/pwmchip3/pwm1/enable", "1");
    writeTo("/sys/class/pwm/pwmchip3/pwm1/duty_cycle", "2000000");
    sleepForMs(1000);
    writeTo("/sys/class/pwm/pwmchip3/pwm1/duty_cycle", "1000000");
}

void userBasedRelease(int timeToSleep){
    int sleep;
    sleep =  timeToSleep * 1000;
    sleepForMs(sleep);
    defaultRelease();
}

void writeI2cReg(int i2cFileDesc, unsigned char regAddr, unsigned char value)
{
    unsigned char buff[2];
    buff[0] = regAddr;
    buff[1] = value;
    int res = write(i2cFileDesc, buff, 2);
    if (res != 2) {
        perror("I2C: Unable to write i2c register.");
        exit(1);
    }
}

int initI2cBus(char* bus, int address)
{
  int i2cFileDesc = open(bus, O_RDWR);
  int result = ioctl(i2cFileDesc, I2C_SLAVE, address);
  if (result < 0) {
    perror("I2C: Unable to set I2C device to slave address.");
    exit(1);
  }
  return i2cFileDesc;
}

void writeSmileyFace(){
    int i2cFileDesc = initI2cBus(I2CDRV_LINUX_BUS1, I2C_DEVICE_ADDRESS);
    writeI2cReg(i2cFileDesc, 0x00, 0x1E);
    writeI2cReg(i2cFileDesc, 0x02, 0x21);
    writeI2cReg(i2cFileDesc, 0x04, 0xD2);
    writeI2cReg(i2cFileDesc, 0x06, 0xD2);
    writeI2cReg(i2cFileDesc, 0x08, 0xC0);
    writeI2cReg(i2cFileDesc, 0x0A, 0xD2);
    writeI2cReg(i2cFileDesc, 0x0C, 0x2D);
    writeI2cReg(i2cFileDesc, 0x0E, 0x1E);
}

void clearDisplay(){
    int i2cFileDesc = initI2cBus(I2CDRV_LINUX_BUS1, I2C_DEVICE_ADDRESS);
    for(int i = 0; i < 16; i+=2){
        writeI2cReg(i2cFileDesc, i, 0x00);
    }
}

void stop_stopServo(){
    servoTurnedOn = false;
    stopServo = true;
    pthread_join(threadServo, NULL);
}

void mode2Release(){
    writeTo("/sys/class/pwm/pwmchip3/pwm1/period", "20000000");
    writeTo("/sys/class/pwm/pwmchip3/pwm1/enable", "1");
    writeTo("/sys/class/pwm/pwmchip3/pwm1/duty_cycle", "2000000");
    sleepForMs(10000);
    writeTo("/sys/class/pwm/pwmchip3/pwm1/duty_cycle", "1000000");
}

static void* turningServoMotor(void* arg){
    while(!stopServo){
        if(mode == 0){
            defaultRelease();
        } else if (mode == 1){
            int timeToSleep;
            printf("Enter a time before releasing food (seconds)\n");
            scanf("%d",&timeToSleep);
            userBasedRelease(timeToSleep);
            sleepForMs(1000);
        } else if (mode == 2){
            mode2Release();
        }
        clearDisplay();
        servoTurnedOn = true;
        sleepForMs(100);
        stop_stopServo();
    }
    return NULL;
}

void start_startServo(){
    stopServo = false;
    pthread_create(&threadServo, NULL, turningServoMotor, NULL);
}

void configureI2C(){
    runCommand("config-pin P9_18 i2c");
    runCommand("config-pin P9_17 i2c");
}

void writeMatrixByBytes(unsigned char* physicalFrameValues){
  int i2cFileDesc = initI2cBus(I2CDRV_LINUX_BUS1, I2C_DEVICE_ADDRESS);
  int j = 0;
  for(int i = 0; i < 16; i += 2){ // for all 8 rows
      writeI2cReg(i2cFileDesc, i, physicalFrameValues[j]);
      j++;
    }
}

void initializeStartRegisters(){
  int i2cFileDesc = initI2cBus(I2CDRV_LINUX_BUS1, I2C_DEVICE_ADDRESS);
  writeI2cReg(i2cFileDesc, SYS_SETUP_REG, 0x00); //write to the system setup register to turn on the matrix.
  writeI2cReg(i2cFileDesc, DISPLAY_SETUP_REG, 0x00); //write to display setup register to turn on LEDs, no flashing.
}

void configureAllPins(){
    runCommand("config-pin p8.15 gpio");
    runCommand("config-pin -q p8.15");
    runCommand("config-pin p8.16 gpio");
    runCommand("config-pin -q p8.16");
    runCommand("config-pin p8.17 gpio");
    runCommand("config-pin -q p8.17");
    runCommand("config-pin p8.18 gpio");
    runCommand("config-pin -q p8.18");
}

typedef struct {
  char digit; // 0-9 or . or empty space
  char rowBitArr[numberOfMatrixRows]; // represents each row of bits of the char
  char cols; // how wide is this character in terms of columns
} matrixData;

static matrixData matrix [] = { // holds all the bit data for each row for every character that may need to be displayed
  {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 4},
  {'0', {0x20, 0x50, 0x50, 0x50, 0x50, 0x50, 0x20, 0x00}, 4},
  {'1', {0x20, 0x30, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00}, 4},
  {'2', {0x20, 0x50, 0x40, 0x20, 0x20, 0x10, 0x70, 0x00}, 4},
  {'3', {0x30, 0x40, 0x40, 0x70, 0x40, 0x40, 0x30, 0x00}, 4},
  {'4', {0x40, 0x60, 0x50, 0x50, 0x70, 0x40, 0x40, 0x00}, 4},
  {'5', {0x70, 0x10, 0x10, 0x70, 0x40, 0x50, 0x20, 0x00}, 4},
  {'6', {0x60, 0x10, 0x10, 0x30, 0x50, 0x50, 0x20, 0x00}, 4},
  {'7', {0x70, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x00}, 4},
  {'8', {0x20, 0x50, 0x50, 0x20, 0x50, 0x50, 0x20, 0x00}, 4},
  {'9', {0x20, 0x50, 0x50, 0x60, 0x40, 0x40, 0x30, 0x00}, 4},
  {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40}, 1},
  {'M', {0x50, 0x70, 0x70, 0x50, 0x50, 0x50, 0x50, 0x00}, 4}
};

static matrixData* searchForHexData(char objectMatrix){ // searches for a char and then returns the address if it is found
  for(int i = 0; matrix[i].digit != EMPTY; i++){
    if (matrix[i].digit == objectMatrix){
      return &matrix[i];
    }
  }
  return NULL;
}

static char shiftLeftOnMatrixBy(int shiftAmountInBytes, char rowValue){ //shiftLeftBy(2,'1')
  if(shiftAmountInBytes >= 0){
    return rowValue >> shiftAmountInBytes;
  }
  else{
    return rowValue << -shiftAmountInBytes;
  }
  return 0;
}

static unsigned char warpFrame(unsigned char logicalFrame){
  unsigned char physicalRows = ((logicalFrame >> 1) | (logicalFrame << 7));
  return physicalRows;
}

static void logicalFrame(){
  for(int i = 0; i < numberOfMatrixRows; i++){
    physicalFrameArr[i] = warpFrame(logicalFrameArr[i]);
  }
  writeMatrixByBytes(physicalFrameArr);
}

static void callMatrixObject(matrixData* currentMatrixData){
  charRowByRowBits = currentMatrixData->rowBitArr; 
  charCurrentColumns = currentMatrixData->cols;
}

static void displayMatrix(char* display){
  //cleaning logicalFrameArr from previous values
  memset(logicalFrameArr,EMPTY, 8);
  char current = ' '; // initialize the current char to be empty
  if(*display != EMPTY){
    current = *display;
  }
  callMatrixObject(searchForHexData(current));
  for(int col = 0; col < numberOfMatrixCols; col+=charCurrentColumns){ // go through all columns
    current = ' '; // initialize the current char to be empty
    if(*display != EMPTY){
      current = *display;
      ++display;      
    }
    callMatrixObject(searchForHexData(current));
    for(int i = 0; i < numberOfMatrixRows; i++){ // for every row
      int shiftAmountInBytes = numberOfMatrixCols - charCurrentColumns - col;
      char rowBits = shiftLeftOnMatrixBy(shiftAmountInBytes,charRowByRowBits[i]); //shift each digit left on the display with a right bitwise shift (>>)
      logicalFrameArr[i] = logicalFrameArr[i] | rowBits;
    }
  }
  logicalFrame();
}

void displayMode(char* c){
  char buff[10];
  snprintf(buff, 10, "%s", c);
  displayMatrix(buff);
}

void switchMode(){
    if(mode == 0){
        mode = 1;
    }
    else if(mode == 1){
        mode = 2;
    }
    else if(mode == 2){
        mode = 0;
    }
}

int readButton(char *button)
{
    FILE *pFile = fopen(button, "r");
    if (pFile == NULL) {
        printf("ERROR: Unable to open file (%s) for read\n", button);
        exit(-1);
    }
    // Read string (line)
    const int MAX_LENGTH = 1024;
    char buff[MAX_LENGTH];
    fgets(buff, MAX_LENGTH, pFile);
    // Close
    fclose(pFile);
    // printf("Read: '%s'\n", buff);
    return(atoi(buff));
}

bool yellowButtonPressed(){
    return (readButton(yellowButtonPath) == 1);
}

void writingToGPIO(float value){
    FILE *pFile = fopen("/sys/class/gpio/export", "w");
    if (pFile == NULL) {
        printf("ERROR: Unable to open export file.\n");
        exit(1);
    }
    fprintf(pFile, "%f", value);
    fclose(pFile);
}

void exportYellowButton(){
    writingToGPIO(27);
}

static void* displayButton(void* arg){
    while(!stopButton){
        clearDisplay();
        if(yellowButtonPressed()){
            //For debounce
            while(yellowButtonPressed()){};
            switchMode();
            sleepForMs(100);
        }
        if(servoTurnedOn){
            writeSmileyFace();
            sleepForMs(5000);
        }
        if(mode == 0){
            displayMode("M0");
        } else if(mode == 1){
            displayMode("M1");
        } else if(mode == 2){
            displayMode("M2");
        }
        sleepForMs(100);
    }
    return NULL;
}

void display_startButton(){
    pthread_create(&threadButton, NULL, displayButton, NULL);
}

void display_stopButton(){
    stopButton = true;
    pthread_join(threadButton, NULL);
}

static void inference_callback(pv_inference_t *inference) {
    fprintf(stdout, "{\n");
    fprintf(stdout, "    is_understood : '%s',\n", (inference->is_understood ? "true" : "false"));
    if (inference->is_understood) {
        fprintf(stdout, "    intent : '%s',\n", inference->intent);
        if (inference->num_slots > 0) {
            fprintf(stdout, "    slots : {\n");
            for (int32_t i = 0; i < inference->num_slots; i++) {
                fprintf(stdout, "        '%s' : '%s',\n", inference->slots[i], inference->values[i]);
            }
            fprintf(stdout, "    }\n");
        }
	
        printf("running servo\n");
        
        start_startServo();
    }
    fprintf(stdout, "}\n\n");
    fflush(stdout);

    pv_inference_delete_func(inference);
}

int picovoice_main(int argc, char *argv[]) {

    signal(SIGINT, interrupt_handler);
    const char *library_path = NULL;
    const char *access_key = NULL;
    const char *keyword_path = NULL;
    const char *context_path = NULL;
    float porcupine_sensitivity = 0.5f;
    const char *porcupine_model_path = NULL;
    float rhino_sensitivity = 0.5f;
    const char *rhino_model_path = NULL;
    float endpoint_duration_sec = 1.f;
    bool require_endpoint = true;
    int32_t device_index = -1;

    int c;
    while ((c = getopt_long(argc, argv, "de:l:a:k:c:s:p:t:r:i:u:", long_options, NULL)) != -1) {
        switch (c) {
            case 'd':
                show_audio_devices();
                return 0;
            case 'l':
                library_path = optarg;
                break;
            case 'a':
                access_key = optarg;
                break;
            case 'k':
                keyword_path = optarg;
                break;
            case 'c':
                context_path = optarg;
                break;
            case 's':
                porcupine_sensitivity = strtof(optarg, NULL);
                break;
            case 'p':
                porcupine_model_path = optarg;
                break;
            case 't':
                rhino_sensitivity = strtof(optarg, NULL);
                break;
            case 'r':
                rhino_model_path = optarg;
                break;
            case 'u':
                endpoint_duration_sec = strtof(optarg, NULL);
                break;
            case 'e':
                if (strcmp(optarg, "false") == 0) {
                    require_endpoint = false;
                }
                break;
            case 'i':
                device_index = (int32_t) strtol(optarg, NULL, 10);
                break;
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }

    if (!library_path || !keyword_path || !context_path || !access_key || !porcupine_model_path || !rhino_model_path) {
        print_usage(argv[0]);
        exit(1);
    }

    void *picovoice_library = open_dl(library_path);
    if (!picovoice_library) {
        fprintf(stderr, "failed to open library.\n");
        exit(1);
    }

    const char *(*pv_status_to_string_func)(pv_status_t) = load_symbol(picovoice_library, "pv_status_to_string");
    if (!pv_status_to_string_func) {
        print_dl_error("failed to load 'pv_status_to_string'");
        exit(1);
    }

    int32_t (*pv_sample_rate_func)() = load_symbol(picovoice_library, "pv_sample_rate");
    if (!pv_sample_rate_func) {
        print_dl_error("failed to load 'pv_sample_rate'");
        exit(1);
    }

    pv_status_t (*pv_picovoice_init_func)(
            const char *,
            const char *,
            const char *,
            float,
            void (*)(void),
            const char *,
            const char *,
            float,
            float,
            bool,
            void (*)(pv_inference_t *),
            pv_picovoice_t **) = NULL;
    pv_picovoice_init_func = load_symbol(picovoice_library, "pv_picovoice_init");
    if (!pv_picovoice_init_func) {
        print_dl_error("failed to load 'pv_picovoice_init'");
        exit(1);
    }

    void (*pv_picovoice_delete_func)(pv_picovoice_t *) = load_symbol(picovoice_library, "pv_picovoice_delete");
    if (!pv_picovoice_delete_func) {
        print_dl_error("failed to load 'pv_picovoice_delete'");
        exit(1);
    }

    pv_status_t (*pv_picovoice_process_func)(pv_picovoice_t *, const int16_t *) =
    load_symbol(picovoice_library, "pv_picovoice_process");
    if (!pv_picovoice_process_func) {
        print_dl_error("failed to load 'pv_picovoice_process'");
        exit(1);
    }

    int32_t (*pv_picovoice_frame_length_func)() = load_symbol(picovoice_library, "pv_picovoice_frame_length");
    if (!pv_picovoice_frame_length_func) {
        print_dl_error("failed to load 'pv_picovoice_frame_length'");
        exit(1);
    }

    const char *(*pv_picovoice_version_func)() = load_symbol(picovoice_library, "pv_picovoice_version");
    if (!pv_picovoice_version_func) {
        print_dl_error("failed to load 'pv_picovoice_version'");
        exit(1);
    }

    pv_inference_delete_func = load_symbol(picovoice_library, "pv_inference_delete");
    if (!pv_inference_delete_func) {
        print_dl_error("failed to load 'pv_inference_delete'");
        exit(1);
    }

    fprintf(stdout, "%s\n", access_key);
    fprintf(stdout, "%s\n", library_path);
    fprintf(stdout, "%s\n", keyword_path);
    fprintf(stdout, "%s\n", context_path);
    fprintf(stdout, "%s\n", access_key);

    pv_picovoice_t *picovoice = NULL;
    pv_status_t status = pv_picovoice_init_func(
            access_key,
            porcupine_model_path,
            keyword_path,
            porcupine_sensitivity,
            wake_word_callback,
            rhino_model_path,
            context_path,
            rhino_sensitivity,
            endpoint_duration_sec,
            require_endpoint,
            inference_callback,
            &picovoice);
    if (status != PV_STATUS_SUCCESS) {
        fprintf(stderr, "'pv_picovoice_init' failed with '%s'\n", pv_status_to_string_func(status));
        exit(1);
    }

    fprintf(stdout, "Picovoice End-to-End Platform (%s) :\n\n", pv_picovoice_version_func());

    const int32_t frame_length = pv_picovoice_frame_length_func();
    pv_recorder_t *recorder = NULL;
    pv_recorder_status_t recorder_status = pv_recorder_init(device_index, frame_length, 100, true, true, &recorder);
    if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to initialize device with %s.\n", pv_recorder_status_to_string(recorder_status));
        exit(1);
    }

    const char *selected_device = pv_recorder_get_selected_device(recorder);
    fprintf(stdout, "Selected device: %s\n", selected_device);


    recorder_status = pv_recorder_start(recorder);
    if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to start device with %s.\n", pv_recorder_status_to_string(recorder_status));
        exit(1);
    }

    int16_t *pcm = malloc(frame_length * sizeof(int16_t));
    if (!pcm) {
        fprintf(stderr, "Failed to allocate pcm memory.\n");
        exit(1);
    }

    fprintf(stdout, "Listening...\n\n");
    fflush(stdout);

    while (!is_interrupted) {
        recorder_status = pv_recorder_read(recorder, pcm);
        if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
            fprintf(stderr, "Failed to read with %s.\n", pv_recorder_status_to_string(recorder_status));
            exit(1);
        }

        status = pv_picovoice_process_func(picovoice, pcm);
        if (status != PV_STATUS_SUCCESS) {
            fprintf(stderr, "'pv_picovoice_process' failed with '%s'\n",
                    pv_status_to_string_func(status));
            exit(1);
        }
    }

    fprintf(stdout, "Stopping...\n");
    fflush(stdout);

    recorder_status = pv_recorder_stop(recorder);
    if (recorder_status != PV_RECORDER_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to stop device with %s.\n", pv_recorder_status_to_string(recorder_status));
        exit(1);
    }

    free(pcm);
    pv_recorder_delete(recorder);
    pv_picovoice_delete_func(picovoice);
    close_dl(picovoice_library);

    return 0;
}

int main(int argc, char *argv[]) {

    configureI2C();
    initializeStartRegisters();
    configureAllPins();
    int i2cFileDesc = initI2cBus(I2CDRV_LINUX_BUS1, I2C_DEVICE_ADDRESS);
    exportYellowButton();

    display_startButton();
#if defined(_WIN32) || defined(_WIN64)

#define UTF8_COMPOSITION_FLAG (0)
#define NULL_TERMINATED (-1)

    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wargv == NULL) {
        fprintf(stderr, "CommandLineToArgvW failed\n");
        exit(1);
    }
    
    char *utf8_argv[argc];

    for (int i = 0; i < argc; ++i) {
        // WideCharToMultiByte: https://docs.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
        int arg_chars_num = WideCharToMultiByte(CP_UTF8, UTF8_COMPOSITION_FLAG, wargv[i], NULL_TERMINATED, NULL, 0, NULL, NULL);
        utf8_argv[i] = (char *) malloc(arg_chars_num * sizeof(char));
        if (!utf8_argv[i]) {
            fprintf(stderr, "failed to to allocate memory for converting args");
        }
        WideCharToMultiByte(CP_UTF8, UTF8_COMPOSITION_FLAG, wargv[i], NULL_TERMINATED, utf8_argv[i], arg_chars_num, NULL, NULL);
    }

    LocalFree(wargv);
    argv = utf8_argv;

#endif

    int result = picovoice_main(argc, argv);

#if defined(_WIN32) || defined(_WIN64)

    for (int i = 0; i < argc; ++i) {
        free(utf8_argv[i]);
    }

#endif
    display_stopButton();
    return result;
}
