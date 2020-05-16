#include <string>
#include <iostream>
#include <queue>
#include <list>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>

using namespace std;

#define ROUNDS 10
#define STOPS 4
#define BUS_LIMIT 50
#define PASSENGERS 250
#define LOG_LVL 1
#define BUS_DELAY 1

class Passenger {
public:
    thread::id id;
    int start;
    int destination;

    Passenger(thread::id id, int start, int destination) : id(id), start(start), destination(destination) {}

    string label() {
        std::stringstream ss;
        ss << id;
        return "Passenger " + ss.str() + " (start:" + to_string(start) + ", destination:" + to_string(destination) +") ";
    }

    Passenger() {}
};
class Bus{
public:
    int currentStop = 1;
    map<thread::id, Passenger> inBus;
    map<thread::id, Passenger> wantOutOnNext;

    int limit = BUS_LIMIT;

    bool isFull(){
        return inBus.size() >= this->limit;;
    }
    bool isEverybodyOutWhoWant() {
        bool is = true;
        for (auto const &x : wantOutOnNext) {
            if (x.second.destination == currentStop) {
                is = false;
            }
        }

        return is;
    }
};
class Stop {
public:
    map<thread::id, Passenger> passengers;

public:
    bool isEmpty() {
        return this->passengers.empty();
    }
};

class BusCommuncation {
private:
    mutex m_mutex;
    condition_variable m_condVar;
public:
    map<int, Stop> stops;
    Bus bus;
    bool isEnd = false;

    bool isBusReadyToGo() {
        bool isEmpty = this->stops[bus.currentStop].isEmpty();
        bool isFull = bus.isFull();

        return isFull || isEmpty;
    }

    bool isBusStopLocked(int busStop) {
        return bus.currentStop == busStop;
    }

    bool isPossibleToEnterTheBus(int busStop) {
        bool isBusOnStop = bus.currentStop == busStop;
        bool isNotFull = !bus.isFull();

        return (isBusOnStop && isNotFull) || this->isEnd;
    }

    bool isReadyToOutOfBus(int busStop) {
        return bus.currentStop == busStop;
    }

    bool isTimeForNotifyDriver(int busStop) {
        if (busStop == 1) {
            return bus.currentStop == STOPS;
        } else {
            return bus.currentStop + 1 == busStop;
        }
    }

    void nextStop() {
        //Autobus jedzie między przystankami
        std::chrono::milliseconds timespan(BUS_DELAY);
        this_thread::sleep_for(timespan);
        //Autobus dojechał do przystanku między przystankami
        unique_lock<mutex> mlock(m_mutex);
        this->bus.currentStop++;
        if (LOG_LVL > 0)
            cout << "Autobus dotarł do przystanku " + to_string(this->bus.currentStop) + "\n";

        m_condVar.notify_all();
        m_condVar.wait(mlock, [this] { return bus.isEverybodyOutWhoWant(); });
        m_condVar.wait(mlock, [this] { return this->isBusReadyToGo(); });
        m_condVar.notify_all();
    }

    void busThread() {
        if (LOG_LVL > 0)
            cout << "Autobus wystartował \n";
        for (int i = 0; i <= ROUNDS; i++) {
            this->bus.currentStop = 0;
            if (LOG_LVL > 0)
                cout << "Autobus rozpoczął rundę nr. " + to_string(i) + "\n";
            while (this->bus.currentStop < STOPS) {
                this->nextStop();
            }
        }
        this->isEnd = true;
        this->bus.currentStop = 0;
    }

    void passengerThread(int start, int destination) {
        // Inicjalizacja - zmierzanie na przystanek
        thread::id id = this_thread::get_id();
        Passenger passenger = Passenger(id, start, destination);
        if (LOG_LVL > 1)
            cout << passenger.label() + "zmierza na przystanek. Czeka na możliwość zajęcia miejsca na przystanku o numerze"<< to_string(start) << "\n";

        // Docieranie do przystanku
        unique_lock<mutex> mlock(m_mutex);
        m_condVar.wait(mlock, [this, start] { return !this->isBusStopLocked(start); });
        if (LOG_LVL > 1)
            cout << passenger.label() + "jest już na przystanku o numerze " << to_string(start) << "\n";
        this->stops[start].passengers[id] = passenger;

        // Wsiadanie do autobusu
        m_condVar.wait(mlock, [this, start] { return this->isPossibleToEnterTheBus(start); });
        if (this->isEnd) {
            if (LOG_LVL > 0)
                cout << passenger.label() + "dzisiaj już nie pojedzie \n";
            m_condVar.notify_all();
        } else {
            if (LOG_LVL > 0)
                cout << passenger.label() + "wsiada \n";
            this->stops[start].passengers.erase(id);
            bus.inBus[id] = passenger;
            m_condVar.notify_all();
            // Wysiadanie z autobusu
            m_condVar.wait(mlock,[this, destination] { return this->isTimeForNotifyDriver(destination) || this->isEnd; });
            if (LOG_LVL > 1)
                cout << passenger.label() + "syngalizuje, że wysiada na następnym przystanku \n";
            bus.wantOutOnNext[id] = passenger;
            m_condVar.wait(mlock, [this, destination] { return this->isReadyToOutOfBus(destination) || this->isEnd; });

            if (this->isEnd) {
                if (LOG_LVL > 0)
                    cout << passenger.label() + "nie dojedzie do celu, bo to ostatnia stacja \n";
                m_condVar.notify_all();
            } else {
                if (LOG_LVL > 0)
                    cout << passenger.label() + "wysiada \n";
                bus.inBus.erase(id);
                bus.wantOutOnNext.erase(id);

                m_condVar.notify_all();
            }
        }
    }
};

int main() {
    BusCommuncation app;
    thread bus{&BusCommuncation::busThread, &app};

    thread passengers[PASSENGERS];
    for (auto &i : passengers) {
        int randStartRide = rand() % STOPS + 1;
        int randEndRide = rand() % STOPS + 1;
        if (randEndRide == randStartRide) {
            if (randStartRide == STOPS) {
                randEndRide = 1;
            } else {
                randEndRide++;
            }
        }
        i = thread(&BusCommuncation::passengerThread, &app, randStartRide, randEndRide);
    }

    bus.join();
    for (auto &passenger : passengers) {
        passenger.join();
    }

    return EXIT_SUCCESS;
}