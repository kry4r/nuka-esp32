// SPDX-License-Identifier: AGPL-3.0-or-later
#include "unity.h"
#include "proto.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void setUp(void){}
void tearDown(void){}
static void examples_and_frame_boundaries(void)
{
    DIR *dir=opendir(NUKA_EXAMPLES); TEST_ASSERT_NOT_NULL(dir);struct dirent *entry;unsigned count=0;
    while((entry=readdir(dir))){
        if(!strstr(entry->d_name,".json"))continue;
        char path[1024];snprintf(path,sizeof(path),"%s/%s",NUKA_EXAMPLES,entry->d_name);FILE *file=fopen(path,"rb");TEST_ASSERT_NOT_NULL(file);
        char text[8193];size_t length=fread(text,1,sizeof(text),file);fclose(file);
        proto_frame_t frame;TEST_ASSERT_TRUE_MESSAGE(proto_parse(text,length,&frame),entry->d_name);proto_frame_free(&frame);++count;
    }
    closedir(dir);TEST_ASSERT_GREATER_THAN(20,count);
    const char *valid="{\"v\":1,\"t\":\"vm.hosts\",\"ts\":0,\"d\":{\"rev\":1,\"items\":[],\"future\":{\"pct\":\"ignored\"}}}";
    proto_frame_t frame;TEST_ASSERT_TRUE(proto_parse(valid,strlen(valid),&frame));proto_frame_free(&frame);
    const char *bad="{\"v\":1,\"t\":\"vm.hosts\",\"ts\":0,\"d\":{\"rev\":1,\"items\":[{\"id\":\"h_1\",\"name\":\"example\",\"online\":true,\"cpu\":{\"pct\":101}}]}}";
    TEST_ASSERT_FALSE(proto_parse(bad,strlen(bad),&frame));
    bad="{\"v\":1,\"t\":\"perm.req\",\"ts\":0,\"d\":{}}";TEST_ASSERT_FALSE(proto_parse(bad,strlen(bad),&frame));
    bad="{\"v\":2,\"t\":\"future\",\"ts\":0,\"d\":{}}";TEST_ASSERT_FALSE(proto_parse(bad,strlen(bad),&frame));
    char oversize[8193];memset(oversize,' ',sizeof(oversize));TEST_ASSERT_FALSE(proto_parse(oversize,sizeof(oversize),&frame));
    valid="{\"v\":1,\"t\":\"future\",\"ts\":0,\"d\":{\"anything\":true}}";TEST_ASSERT_TRUE(proto_parse(valid,strlen(valid),&frame));proto_frame_free(&frame);
    bad="{\"v\":1,\"t\":\"future\",\"ts\":0,\"d\":{\"nested\":[{\"x\\u0000y\":\"value\"}]}}";TEST_ASSERT_FALSE(proto_parse(bad,strlen(bad),&frame));
    bad="{\"v\":1,\"t\":\"future\",\"ts\":0,\"d\":{\"text\":\"prefix\\u0000suffix\"}}";TEST_ASSERT_FALSE(proto_parse(bad,strlen(bad),&frame));
    valid="{\"v\":1,\"t\":\"future\",\"ts\":0,\"d\":{\"text\":\"literal \\\\u0000\"}}";TEST_ASSERT_TRUE(proto_parse(valid,strlen(valid),&frame));proto_frame_free(&frame);
}
int main(void){UNITY_BEGIN();RUN_TEST(examples_and_frame_boundaries);return UNITY_END();}
