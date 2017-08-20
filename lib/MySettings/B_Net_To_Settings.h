#ifdef _DEBUG_ || _TEST_

const char blynk_auth[] = "b8d3d31fd7de4256a7f8abe34f34dd2b";
const char* blynk_host = "blynk-cloud.com";
unsigned int blynk_port = 8080; // to test error handdling
const char *CSsid = "eir83741049-2.4G";
const char *CPwd = "je5x52ec";
const char *CDSsid = "Plant_";
const char *CDPwd = "changeme!";
const char *CHost = "plant.io";

#else

const char blynk_auth[] = "b8d3d31fd7de4256a7f8abe34f34dd2b";
const char* blynk_host = "blynk-cloud.com";
unsigned int blynk_port = 8080;
const char *CSsid = "eir83741049-2.4G";
const char *CPwd = "je5x52ec";
const char *CDSsid = "Plant_";
const char *CDPwd = "changeme!";
const char *CHost = "plant.io";

#endif
