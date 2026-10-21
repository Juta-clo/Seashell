#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

void unity_output_cb(char ch);

#define UNITY_OUTPUT_CHAR(ch) unity_output_cb(ch)

#endif
