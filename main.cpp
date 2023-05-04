#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <queue>
#include <csignal>

std::mutex mtx;
std::condition_variable cv;
bool threadIsfFinished = false;

const int MIN_REQUEST_TYPE = 1;
const int MAX_REQUEST_TYPE = 3;

const int MIN_DEVICE_SLEEP_TIME = 2000;
const int MAX_DEVICE_SLEEP_TIME = 4000;

const int MIN_GEN_SLEEP_TIME = 500;
const int MAX_GEN_SLEEP_TIME = 1500;

enum TimeType {
    DeviceTime,
    GenTime
};

void signalHandler(int signal) {
    threadIsfFinished = true;
    cv.notify_all();
}

int getRandomNumber(int start, int end) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(start, end);

    return dist(gen);
}

std::chrono::milliseconds getRandomSleepTime(TimeType timeType) {
    return timeType == DeviceTime
           ? []() -> std::chrono::milliseconds {
                return std::chrono::milliseconds(getRandomNumber(MIN_DEVICE_SLEEP_TIME,
                                                                    MAX_DEVICE_SLEEP_TIME));
            }() : []() -> std::chrono::milliseconds {
                return std::chrono::milliseconds (getRandomNumber(MIN_GEN_SLEEP_TIME,
                                                                    MAX_GEN_SLEEP_TIME));
            }();
}

struct Device {
    int groupId;
    int id;
};

struct Request {
    int groupId;
    int type;

    bool operator<(const Request& other) const {
        return type < other.type;
    }

    bool operator>(const Request& other) const {
        return type > other.type;
    }
};

class RequestQueue {
public:
    RequestQueue(unsigned int capacity, unsigned int numOfGroups) {
        this->currentSize = 0;
        this->capacity = capacity;
        initializeQueues(numOfGroups);
    }

    unsigned int size() const {
        return currentSize;
    }

    Request top(int groupId) const {
        return allRequests[groupId][0];
    }

    bool isFull() const {
        return currentSize == capacity;
    }

    bool isEmpty(int groupId) const {
        return allRequests[groupId].empty();
    }

    void push(Request request) {
        currentSize++;
        allRequests[request.groupId].push_back(request);
        heapifyUp(allRequests[request.groupId], allRequests[request.groupId].size() - 1);
    }

    void pop(int groupId) {
        currentSize--;
        allRequests[groupId][0] = allRequests[groupId].back();
        allRequests[groupId].pop_back();
        heapifyDown(allRequests[groupId], 0);
    }

private:
    unsigned int capacity;
    unsigned int currentSize;
    std::vector<std::vector<Request>> allRequests;

    void initializeQueues(unsigned int size) {
        for (size_t i = 0; i < size; i++) {
            std::vector<Request> requests;
            allRequests.push_back(requests);
        }
    }

    void heapifyUp(std::vector<Request>& requests, unsigned int index) {
        if (index == 0) return;
        unsigned int parent = (index - 1) / 2;
        if (!(requests[parent] < requests[index])) return;

        std::swap(requests[parent], requests[index]);
        heapifyUp(requests, parent);
    }

    void heapifyDown(std::vector<Request>& requests, unsigned int index) {
        unsigned int large = index;
        unsigned int left = 2 * index + 1;
        unsigned int right = 2 * index + 2;

        if (left < requests.size() && requests[left] > requests[large]) {
            large = left;
        }

        if (right < requests.size() && requests[right] > requests[large]) {
            large = right;
        }

        if (large == index) return;
        std::swap(requests[index], requests[large]);
        heapifyDown(requests, large);
    }
};

void requestProcessing(Device device, RequestQueue& queue) {
    while (true) {
        std::unique_lock<std::mutex> q_lock(mtx);
        cv.wait(q_lock, [&]() -> bool {
            return !queue.isEmpty(device.groupId) || threadIsfFinished;
        });

        if (threadIsfFinished) break;

        Request request = queue.top(device.groupId);
        queue.pop(device.groupId);
        std::chrono::milliseconds sleepTime = getRandomSleepTime(DeviceTime);

        std::cout << "Device " << device.id + 1 << " (group " << device.groupId + 1 << ") "
                  << "is processing the request " << "(type " << request.type << ")" << " from group "
                  << request.groupId + 1 << std::endl << "Device awakening after " << sleepTime.count()
                  << " ms." << std::endl << std::endl;

        q_lock.unlock();
        cv.notify_all();
        std::this_thread::sleep_for(sleepTime);
    }

    std::lock_guard<std::mutex> lg(mtx);
    std::cout << "Device " << device.id + 1 << " thread is terminating" << std::endl;
}

void generateRequest(RequestQueue& queue, int numGroups) {
    while (true) {
        std::unique_lock q_lock(mtx);
        cv.wait(q_lock, [&]() -> bool {
            return !queue.isFull() || threadIsfFinished;
        });

        if (threadIsfFinished) break;

        queue.push(Request(getRandomNumber(0, numGroups - 1),
                           getRandomNumber(MIN_REQUEST_TYPE, MAX_REQUEST_TYPE)));

        std::cout << "Queue size: " << queue.size() << std::endl;

        q_lock.unlock();
        cv.notify_all();
        std::this_thread::sleep_for(getRandomSleepTime(GenTime));
    }

    std::lock_guard<std::mutex> lg(mtx);
    std::cout << "Generator thread is terminating" << std::endl;
}

int main() {
    signal(SIGINT, signalHandler);

    unsigned int capacity;
    unsigned int numOfGroups;
    unsigned int numOfDevices;

    std::cout << "Enter the queue capacity: ";
    std::cin >> capacity;

    std::cout << "Enter the number of groups: ";
    std::cin >> numOfGroups;

    std::cout << "Enter the number of devices in groups: ";
    std::cin >> numOfDevices;

    RequestQueue requestQueue(capacity, numOfGroups);
    std::vector<std::thread> devicesThreads;

    for (unsigned int i = 0; i < numOfGroups; i++) {
        for (unsigned int q = 0; q < numOfDevices; q++) {
            Device device(i, i * numOfDevices + q);
            devicesThreads.emplace_back(requestProcessing, device, std::ref(requestQueue));
        }
    }

    std::thread generatorThread(generateRequest, std::ref(requestQueue), numOfGroups);

    generatorThread.join();
    for (std::thread& deviceThread : devicesThreads) { deviceThread.join(); }

    std::cout << "Main thread finished";
}
