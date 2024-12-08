CFLAGS = -g -O0
CXXFLAGS = -g -O0
LDFLAGS = -g -O0

.PHONY: runtests
runtests: zabbix_test
	./zabbix_test


.PHONY: clean
clean:
	$(RM) a.out *.o

zabbix_test: zabbix_test.o tiny-json.o
	$(CXX) $(LDFLAGS) -o $@ $^

zabbix_test.o: zabbix.cpp
	$(CXX) $(CPPFLAGS) -DRUNTESTS=1 $(CXXFLAGS) -c -o zabbix_test.o zabbix.cpp

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $^


.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $^

