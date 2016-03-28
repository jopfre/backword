# C++ Compiler (Default: g++)
CXX = g++ -std=c++11 -static
CFLAGS = -Wall -Wextra -pedantic -DCURL_STATICLIB

# Librarys
INCLUDE = -Iusr/local/include -Itmp/curl/include
LDFLAGS = -Lusr/local/lib -L/tmp/curl/lib
LDLIBS = -lcurl

# Details
SOURCES = /tmp/curl/lib/libcurl.a lodepng.cpp backword.cc
OUT = backword

all: build

clean:
		rm -f backword

build: $(SOURCES)
		$(CXX) $(SOURCES) -o $(OUT) $(INCLUDE) $(CFLAGS) $(LDFLAGS) $(LDLIBS) -O3 
