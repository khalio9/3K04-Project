# Build and test the ADC (Part A).   make        -> ./adc_demo  (the demo)
#                                    make test   -> runs every test program
CXX      ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Iadc

SRC = adc/EventLog.cpp adc/CommManager.cpp adc/Protocol.cpp adc/MsSim.cpp \
      adc/access_control.cpp adc/inventory_manager.cpp adc/dispense_controller.cpp \
      adc/SimHardware.cpp adc/DrawerSensor.cpp adc/LockDriver.cpp adc/ModeManager.cpp \
      adc/FaultMonitor.cpp adc/CabinetPlatform.cpp

adc_demo: main.cpp adc/Ui.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp adc/Ui.cpp $(SRC)

TESTS = test_eventlog test_comm test_areeb test_hardware test_integration

test_eventlog: tests/TestEventLog.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^
test_comm: tests/TestCommManager.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^
test_areeb: tests/tests\ (AC,\ DC,\ IM).cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ "tests/tests (AC, DC, IM).cpp" $(SRC)
test_hardware: tests/TestHardwareModes.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^
test_integration: tests/TestIntegration.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: $(TESTS)
	./test_eventlog && ./test_comm && ./test_areeb && ./test_hardware && ./test_integration

clean:
	rm -f adc_demo $(TESTS) *.txt *.tmp

.PHONY: test clean
