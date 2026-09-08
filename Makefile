CXX = g++
CXXFLAGS_BASE = -Wall -Wextra -std=c++20 -I. $(shell pkg-config --cflags gtk+-3.0)
CXXFLAGS = $(CXXFLAGS_BASE) -O2
CXXFLAGS_DEBUG = $(CXXFLAGS_BASE) -g -O0 -DDEBUG
CXXFLAGS_TEST = -Wall -Wextra -std=c++20 -O2 -I. -DRALLY_NO_GTK
LDFLAGS = $(shell pkg-config --libs gtk+-3.0) -lasound -lpthread -ldl
TARGET = HistoricRallyMeter
TARGET_DEBUG = HistoricRallyMeter_debug
TEST_TARGET = run_tests

# Main application sources
SOURCES = main.cpp i2c_counter.cpp rally_state.cpp config_file.cpp counter_poller.cpp \
          calculations.cpp ui_driver.cpp ui_copilot.cpp callbacks.cpp tone_generator.cpp \
          simple_tone.cpp arrow_tone.cpp \
          webserver/rally_web_server.cpp webserver/web_telemetry.cpp webserver/web_commands.cpp \
          webserver/qr_display.cpp
OBJECTS = $(SOURCES:.cpp=.o)
OBJECTS_DEBUG = $(SOURCES:.cpp=_debug.o)

# Header dependencies — changing any header triggers full rebuild
HEADERS = $(wildcard *.h)

# Test sources (calculations, rally_state, config_file for unit tests)
TEST_SOURCES = tests/test_main.cpp calculations.cpp rally_state.cpp config_file.cpp simple_tone.cpp arrow_tone.cpp
TEST_OBJECTS = tests/test_main.o calculations_test.o rally_state_test.o config_file_test.o \
               counter_poller_test.o simple_tone_test.o arrow_tone_test.o

# Default target
all: $(TARGET)

# Debug target
debug: $(TARGET_DEBUG)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

$(TARGET_DEBUG): $(OBJECTS_DEBUG)
	$(CXX) $(CXXFLAGS_DEBUG) -o $(TARGET_DEBUG) $(OBJECTS_DEBUG) $(LDFLAGS)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

%_debug.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS_DEBUG) -c $< -o $@

# Test targets
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS_TEST) -o $(TEST_TARGET) $(TEST_OBJECTS)

# $(HEADERS) as well as the test headers: without it a change to rally_state.h
# rebuilt config_file_test.o but NOT this object, leaving two translation units
# with different ideas of RallyState's layout linked together -- which shows up
# as heap corruption ("free(): invalid pointer") rather than a build error.
tests/test_main.o: tests/test_main.cpp tests/*.h $(HEADERS)
	$(CXX) $(CXXFLAGS_TEST) -c tests/test_main.cpp -o tests/test_main.o

calculations_test.o: calculations.cpp calculations.h rally_state.h rally_types.h
	$(CXX) $(CXXFLAGS_TEST) -c calculations.cpp -o calculations_test.o

rally_state_test.o: rally_state.cpp rally_state.h rally_types.h
	$(CXX) $(CXXFLAGS_TEST) -c rally_state.cpp -o rally_state_test.o

config_file_test.o: config_file.cpp config_file.h rally_state.h rally_types.h
	$(CXX) $(CXXFLAGS_TEST) -c config_file.cpp -o config_file_test.o

counter_poller_test.o: counter_poller.cpp counter_poller.h i_counter.h rally_types.h
	$(CXX) $(CXXFLAGS_TEST) -c counter_poller.cpp -o counter_poller_test.o

simple_tone_test.o: simple_tone.cpp simple_tone.h
	$(CXX) $(CXXFLAGS_TEST) -c simple_tone.cpp -o simple_tone_test.o

arrow_tone_test.o: arrow_tone.cpp arrow_tone.h calculations.h
	$(CXX) $(CXXFLAGS_TEST) -c arrow_tone.cpp -o arrow_tone_test.o

clean:
	rm -f $(TARGET) $(TARGET_DEBUG) $(OBJECTS) $(OBJECTS_DEBUG) $(TEST_TARGET) $(TEST_OBJECTS)

.PHONY: all clean test debug
