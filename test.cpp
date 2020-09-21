#include <iostream>
#include "ntp_client.hpp"
int main() {
	std::string str;
    G_NTP_CLIENT.get_ntp_time("110.12.66.120", str);
    std::cout << str << std::endl;
 }