/* This  file is modified from source available at http://www.kernel.org/pub/linux/utils/cpu/msr-tools/
 for Model specific cpu registers
 Modified to take i7 into account by Abhishek Jaiantilal abhishek.jaiantilal@colorado.edu

// Information about i7's MSR in
// http://download.intel.com/design/processor/applnots/320354.pdf
// Appendix B of http://www.intel.com/Assets/PDF/manual/253669.pdf

//about rdmsr
#ident "$Id: rdmsr.c,v 1.4 2004/07/20 15:54:59 hpa Exp $"
 ----------------------------------------------------------------------- *
 *
 *   Copyright 2000 Transmeta Corporation - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *   Boston, MA 02110-1301, USA;
 *   either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */
#include <memory.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <math.h>
#include <cpuid.h>
#include <ncurses.h>
#include "i7z.h"

extern struct program_options prog_options;
bool E7_mp_present=false;

// Read temperature
#define IA32_THERM_STATUS 0x19C
#define IA32_TEMPERATURE_TARGET 0x1a2
#define IA32_PACKAGE_THERM_STATUS 0x1b1

int Get_Bits_Value(unsigned long val,int highbit, int lowbit){
    unsigned long data = val;
    int bits = highbit - lowbit + 1;
    if(bits<64){
        data >>= lowbit;
        data &= (1ULL<<bits) - 1;
    }
    return(data);
}

// a nice document to read is 322683.pdf from intel
int Read_Thermal_Status_CPU(int cpu_num){
    int error_indx;
    unsigned long val= get_msr_value(cpu_num,IA32_THERM_STATUS,63,0,&error_indx);
    int digital_readout = Get_Bits_Value(val,23,16);
    bool thermal_status = Get_Bits_Value(val,32,31);

        val= get_msr_value(cpu_num,IA32_TEMPERATURE_TARGET,63,0,&error_indx);
        int PROCHOT_temp = Get_Bits_Value(val,23,16);

    //temperature is prochot - digital readout
    if (thermal_status)
      return(PROCHOT_temp - digital_readout);
    else
      return(-1);
}

#define MSR_PERF_STATUS 0x198

float Read_Voltage_CPU(int cpu_num){
    int error_indx;
    unsigned long val = get_msr_value(cpu_num,MSR_PERF_STATUS,47,32,&error_indx);
    return (float)val / (float)(1 << 13);
}

void print_family_info (struct family_info *proc_info)
{
    //print CPU info
    printf ("i7z DEBUG:    Stepping %x\n", proc_info->stepping);
    printf ("i7z DEBUG:    Model %x\n", proc_info->model);
    printf ("i7z DEBUG:    Family %x\n", proc_info->family);
    printf ("i7z DEBUG:    Processor Type %x\n", proc_info->processor_type);
    printf ("i7z DEBUG:    Extended Model %x\n", proc_info->extended_model);
    //    printf("    Extended Family %x\n", (short int*)(&proc_info->extended_family));
    //    printf("    Extended Family %d\n", proc_info->extended_family);
}

void init_ncurses()
{
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    start_color();             /* initialize colors */
    use_default_colors ();
    init_pair (1, COLOR_GREEN, -1);
    init_pair (2, COLOR_YELLOW, -1);
    init_pair (3, COLOR_RED, -1);
    init_pair (4, COLOR_WHITE, -1);
}

static inline void get_vendor (char *vendor_string)
{
    //get vendor name
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __get_cpuid (eax, &eax, &ebx, &ecx, &edx);
    memcpy (vendor_string, &ebx, 4);
    memcpy (vendor_string + 4, &edx, 4);
    memcpy (vendor_string + 8, &ecx, 4);
    vendor_string[12] = '\0';
    //        printf("Vendor %s\n",vendor_string);
}

int turbo_status ()
{
    //turbo state flag
    unsigned int eax = 6, ebx, ecx, edx;
    __get_cpuid (eax, &eax, &ebx, &ecx, &edx);
    return ((eax & 0x2) >> 1);
}

static inline void get_familyinformation (struct family_info *proc_info)
{
    //get info about CPU
    unsigned int eax = 1, ebx, ecx, edx;
    __get_cpuid (eax, &eax, &ebx, &ecx, &edx);
    //  printf ("eax %x\n", b);
    proc_info->stepping = eax & 0x0000000F;    //bits 3:0
    proc_info->model = (eax & 0x000000F0) >> 4;    //bits 7:4
    proc_info->family = (eax & 0x00000F00) >> 8;    //bits 11:8
    proc_info->processor_type = (eax & 0x00007000) >> 12;    //bits 13:12
    proc_info->extended_model = (eax & 0x000F0000) >> 16;    //bits 19:16
    proc_info->extended_family = (eax & 0x0FF00000) >> 20;    //bits 27:20
}

double estimate_MHz ()
{
    //copied blantantly from http://www.cs.helsinki.fi/linux/linux-kernel/2001-37/0256.html
    /*
    * $Id: MHz.c,v 1.4 2001/05/21 18:58:01 davej Exp $
    * This file is part of x86info.
    * (C) 2001 Dave Jones.
    *
    * Licensed under the terms of the GNU GPL License version 2.
    *
    * Estimate CPU MHz routine by Andrea Arcangeli <andrea@suse.de>
    * Small changes by David Sterba <sterd9am@ss1000.ms.mff.cuni.cz>
    *
    */
    struct timezone tz;
    struct timeval tvstart, tvstop;
    unsigned long long int cycles[2];        /* must be 64 bit */
    unsigned long long int microseconds;    /* total time taken */

    memset (&tz, 0, sizeof (tz));

    /* get this function in cached memory */
    gettimeofday (&tvstart, &tz);
    cycles[0] = rdtsc ();
    gettimeofday (&tvstart, &tz);

    /* we don't trust that this is any specific length of time */
    /*1 sec will cause rdtsc to overlap multiple times perhaps. 100msecs is a good spot */
    usleep (10000);

    cycles[1] = rdtsc ();
    gettimeofday (&tvstop, &tz);
    microseconds = ((tvstop.tv_sec - tvstart.tv_sec) * 1000000) +
                   (tvstop.tv_usec - tvstart.tv_usec);

    unsigned long long int elapsed = 0;
    if (cycles[1] < cycles[0])
    {
        //printf("c0 = %llu   c1 = %llu",cycles[0],cycles[1]);
        elapsed = UINT32_MAX - cycles[0];
        elapsed = elapsed + cycles[1];
        //printf("c0 = %llu  c1 = %llu max = %llu elapsed=%llu\n",cycles[0], cycles[1], UINT32_MAX,elapsed);
    }
    else
    {
        elapsed = cycles[1] - cycles[0];
        //printf("\nc0 = %llu  c1 = %llu elapsed=%llu\n",cycles[0], cycles[1],elapsed);
    }

    double mhz = elapsed / microseconds;


    //printf("%llg MHz processor (estimate).  diff cycles=%llu  microseconds=%llu \n", mhz, elapsed, microseconds);
    //printf("%g  elapsed %llu  microseconds %llu\n",mhz, elapsed, microseconds);
    return (mhz);
}

uint64_t get_msr_value (int cpu, uint32_t reg, unsigned int highbit,
                        unsigned int lowbit, int* error_indx)
{
    uint64_t data;
    int fd;
    //  char *pat;
    //  int width;
    char msr_file_name[64];
    int bits;
    *error_indx =0;

    sprintf (msr_file_name, "/dev/cpu/%d/msr", cpu);
    fd = open (msr_file_name, O_RDONLY);
    if (fd < 0)
    {
        if (errno == ENXIO)
        {
            //fprintf (stderr, "rdmsr: No CPU %d\n", cpu);
            *error_indx = 1;
            return 1;
        } else if (errno == EIO) {
            //fprintf (stderr, "rdmsr: CPU %d doesn't support MSRs\n", cpu);
            *error_indx = 1;
            return 1;
        } else {
            //perror ("rdmsr:open");
            *error_indx = 1;
            return 1;
            //exit (127);
        }
    }

    if (pread (fd, &data, sizeof data, reg) != sizeof data)
    {
        perror ("rdmsr:pread");
        exit (127);
    }

    close (fd);

    bits = highbit - lowbit + 1;
    if (bits < 64)
    {
        /* Show only part of register */
        data >>= lowbit;
        data &= (1ULL << bits) - 1;
    }

    /* Make sure we get sign correct */
    if (data & (1ULL << (bits - 1)))
    {
        data &= ~(1ULL << (bits - 1));
        data = -data;
    }

    *error_indx = 0;
    return (data);
}

uint64_t set_msr_value (int cpu, uint32_t reg, uint64_t data)
{
    int fd;
    char msr_file_name[64];

    sprintf (msr_file_name, "/dev/cpu/%d/msr", cpu);
    fd = open (msr_file_name, O_WRONLY);
    if (fd < 0)
    {
        if (errno == ENXIO)
        {
            fprintf (stderr, "wrmsr: No CPU %d\n", cpu);
            exit (2);
        } else if (errno == EIO) {
            fprintf (stderr, "wrmsr: CPU %d doesn't support MSRs\n", cpu);
            exit (3);
        } else {
            perror ("wrmsr:open");
            exit (127);
        }
    }

    if (pwrite (fd, &data, sizeof data, reg) != sizeof data)
    {
        perror ("wrmsr:pwrite");
        exit (127);
    }
    close(fd);
    return(1);
}


//Below code
/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2010 Abhishek Jaiantilal
 *
 *   Under GPL v2
 *
 * ----------------------------------------------------------------------- */


// sets processor version
void Print_Information_Processor(bool* nehalem, bool* sandy_bridge, bool* ivy_bridge, bool* haswell)
{
    struct family_info proc_info;

    char vendor_string[13];
    get_vendor (vendor_string);

    if (strcmp (vendor_string, "GenuineIntel") == 0) {
        printf ("i7z DEBUG: Found Intel Processor\n");
    } else {
        printf ("Intel processor was not detected in CPUID\n");
        exit (1);
    }

    get_familyinformation (&proc_info);
    print_family_info (&proc_info);

    debug(prog_options.quiet, "msr = Model Specific Register");
    print_model(prog_options.quiet, proc_info.model, proc_info.extended_model);
    if (proc_info.family >= 0x6) {
        if (proc_info.extended_model >= 0x3) {
            if (proc_info.model == 0xA) {
                *ivy_bridge = true;
            } else if (proc_info.model == 0xC) {
                *haswell = true;
            } else *haswell = true; //FIXME
        } else if ((proc_info.model == 0xA || proc_info.model == 0xD) && proc_info.extended_model == 0x2) {
            *sandy_bridge = true;
        } else *nehalem = true;
    } else {
        error("Unknown, possibly pre i7 processor detected.");
        exit (1);
    }
}

void Test_Or_Make_MSR_DEVICE_FILES()
{
    //test if the msr file exists
    if (faccessat (0, "/dev/cpu/0/msr", F_OK, AT_EACCESS) == 0)
    {
        printf ("i7z DEBUG: msr device files exist /dev/cpu/*/msr\n");
        if (faccessat (0, "/dev/cpu/0/msr", W_OK, AT_EACCESS) == 0)
        {
            //a system mght have been set with msr allowable to be written
            //by a normal user so...
            //Do nothing.
            printf ("i7z DEBUG: You have write permissions to msr device files\n");
        } else {
            printf ("i7z DEBUG: You DO NOT have write permissions to msr device files\n");
            printf ("i7z DEBUG: A solution is to run this program as root\n");
            exit (1);
        }
    } else {
        printf ("i7z DEBUG: msr device files DO NOT exist, trying out a makedev script\n");
        if (geteuid () == 0)
        {
            //Try the Makedev script
            //sourced from MAKEDEV-cpuid-msr script in msr-tools
            system ("msr_major=202; \
                            cpuid_major=203; \
                            n=0; \
                            while [ $n -lt 16 ]; do \
                                mkdir -m 0755 -p /dev/cpu/$n; \
                                mknod /dev/cpu/$n/msr -m 0600 c $msr_major $n; \
                                mknod /dev/cpu/$n/cpuid -m 0444 c $cpuid_major $n; \
                                n=`expr $n + 1`; \
                            done; \
                            ");
            printf ("i7z DEBUG: modprobbing for msr\n");
            system ("modprobe msr");
        } else {
            printf ("i7z DEBUG: You DO NOT have root privileges, mknod to create device entries won't work out\n");
            printf ("i7z DEBUG: A solution is to run this program as root\n");
            exit (1);
        }
    }
}
double cpufreq_info()
{
    //CPUINFO is wrong for i7 but correct for the number of physical and logical cores present
    //If Hyperthreading is enabled then, multiple logical processors will share a common CORE ID
    //http://www.redhat.com/magazine/022aug06/departments/tips_tricks/
    system
    ("cat /proc/cpuinfo |grep MHz|sed 's/cpu\\sMHz\\s*:\\s//'|tail -n 1 > /tmp/cpufreq.txt");


    //Open the parsed cpufreq file and obtain the cpufreq from /proc/cpuinfo
    FILE *tmp_file;
    tmp_file = fopen ("/tmp/cpufreq.txt", "r");
    char tmp_str[30];
    fgets (tmp_str, 30, tmp_file);
    fclose (tmp_file);
    return atof(tmp_str);
}

int check_and_return_processor(char*strinfo)
{
    char *t1;
    if (strstr(strinfo,"processor") !=NULL) {
        strtok(strinfo,":");
        t1 = strtok(NULL, " ");
        return(atoi(t1));
    } else {
        return(-1);
    }
}

int check_and_return_physical_id(char*strinfo)
{
    char *t1;
    if (strstr(strinfo,"physical id") !=NULL) {
        strtok(strinfo,":");
        t1 = strtok(NULL, " ");
        return(atoi(t1));
    } else {
        return(-1);
    }
}

int check_and_return_core_id(char*strinfo)
{
    char *t1;
    if (strstr(strinfo,"core id") !=NULL) {
        strtok(strinfo,":");
        t1 = strtok(NULL, " ");
        return(atoi(t1));
    } else {
        return(-1);
    }
}

void construct_sibling_list(struct cpu_hierarchy_info* chi)
{
    int i,j,core_id,socket_id;
    for (i=0;i< chi->max_online_cpu ;i++) {
        assert(i < MAX_HI_PROCESSORS);
        chi->sibling_num[i]=-1;
    }

    chi->HT=false;
    for (i=0;i< chi->max_online_cpu ;i++) {
        assert(i < MAX_HI_PROCESSORS);
        core_id = chi->coreid_num[i];
        socket_id = chi->package_num[i];
        for (j=i+1;j< chi->max_online_cpu ;j++) {
            assert(j < MAX_HI_PROCESSORS);
            if (chi->coreid_num[j] == core_id && chi->package_num[j] == socket_id) {
                chi->sibling_num[j] = i;
                chi->sibling_num[i] = j;
                chi->display_cores[i] = 1;
                chi->display_cores[j] = -1;
                chi->HT=true;
                continue;
            }
        }
    }
    //for cores that donot have a sibling put in 1
    for (i=0;i< chi->max_online_cpu ;i++) {
        assert(i < MAX_HI_PROCESSORS);
        if (chi->sibling_num[i] ==-1)
            chi->display_cores[i] = 1;
    }
}

void construct_socket_information(struct cpu_hierarchy_info* chi,
    struct cpu_socket_info* socket_0,struct cpu_socket_info* socket_1,
    int socket_0_num, int socket_1_num)
{
    int i;

    socket_0->max_cpu=0;
    socket_0->num_physical_cores=0;
    socket_0->num_logical_cores=0;
    socket_1->max_cpu=0;
    socket_1->num_physical_cores=0;
    socket_1->num_logical_cores=0;


    for (i=0;i< chi->max_online_cpu ;i++) {
        assert(i < MAX_HI_PROCESSORS);
        if (chi->display_cores[i]!=-1) {
            if (chi->package_num[i]==socket_0_num) {
                assert(socket_0->max_cpu < MAX_SK_PROCESSORS);
                socket_0->processor_num[socket_0->max_cpu]=chi->processor_num[i];
                socket_0->max_cpu++;
                socket_0->num_physical_cores++;
                socket_0->num_logical_cores++;
            }
            if (chi->package_num[i]==socket_1_num) {
                assert(socket_1->max_cpu < MAX_SK_PROCESSORS);
                socket_1->processor_num[socket_1->max_cpu]=chi->processor_num[i];
                socket_1->max_cpu++;
                socket_1->num_physical_cores++;
                socket_1->num_logical_cores++;
            }
        } else {
            if (chi->package_num[i]==socket_0_num) {
                socket_0->num_logical_cores++;
            }
            if (chi->package_num[i]==socket_1_num) {
                socket_1->num_logical_cores++;
            }
        }
    }
}

void print_socket_information(struct cpu_socket_info* socket)
{
    int i;
    char socket_list[200]="";

    for (i=0;i< socket->max_cpu ;i++) {
        assert(i < MAX_SK_PROCESSORS);
        if (socket->processor_num[i]!=-1) {
            sprintf(socket_list,"%s%d,",socket_list,socket->processor_num[i]);
        }
    }
    printf("Socket-%d [num of cpus %d physical %d logical %d] %s\n",socket->socket_num,socket->max_cpu,socket->num_physical_cores,socket->num_logical_cores,socket_list);
}

void construct_CPU_Hierarchy_info(struct cpu_hierarchy_info* chi)
{
    FILE *fp = fopen("/proc/cpuinfo","r");
    char strinfo[200];

    int processor_num, physicalid_num, coreid_num;
    int it_processor_num=-1, it_physicalid_num=-1, it_coreid_num=-1;
    int tmp_processor_num, tmp_physicalid_num, tmp_coreid_num;
    int old_processor_num=-1;

    memset(chi, 0, sizeof(*chi));

    if (fp!=NULL) {
        while ( fgets(strinfo,200,fp) != NULL) {
            //        printf(strinfo);
            tmp_processor_num = check_and_return_processor(strinfo);
            tmp_physicalid_num = check_and_return_physical_id(strinfo);
            tmp_coreid_num = check_and_return_core_id(strinfo);


            if (tmp_processor_num != -1) {
                it_processor_num++;
                processor_num = tmp_processor_num;
                assert(it_processor_num < MAX_HI_PROCESSORS);
                chi->processor_num[it_processor_num] = processor_num;
            }
            if (tmp_physicalid_num != -1) {
                it_physicalid_num++;
                physicalid_num = tmp_physicalid_num;
                assert(it_physicalid_num < MAX_HI_PROCESSORS);
                chi->package_num[it_physicalid_num] = physicalid_num;
            }
            if (tmp_coreid_num != -1) {
                it_coreid_num++;
                coreid_num = tmp_coreid_num;
                assert(it_coreid_num < MAX_HI_PROCESSORS);
                chi->coreid_num[it_coreid_num] = coreid_num;
            }
            if (processor_num != old_processor_num) {
                old_processor_num = processor_num;
            }
        }
    }
    chi->max_online_cpu = it_processor_num+1;
    fclose(fp);
}

void print_CPU_Hierarchy(struct cpu_hierarchy_info chi)
{
    int i;
    printf("\n------------------------------\n--[core id]--- Other information\n-------------------------------------\n");
    for (i=0;i < chi.max_online_cpu;i++) {
        assert(i < MAX_HI_PROCESSORS);
        printf("--[%d] Processor number %d\n",i,chi.processor_num[i]);
        printf("--[%d] Socket number/Hyperthreaded Sibling number  %d,%d\n",i,chi.package_num[i],chi.sibling_num[i]);
        printf("--[%d] Core id number %d\n",i,chi.coreid_num[i]);
        printf("--[%d] Display core in i7z Tool: %s\n\n",i,(chi.display_cores[i]==1)?"Yes":"No");
    }
}

int in_core_list(int ii,int* core_list)
{
    int i;
    int in=0;
    for (i=0;i<8;i++) {
        if (ii == core_list[i]) {
            in=1;
            break;
        }
    }
    return(in);
}

bool file_exists(char* filename)
{
    if (faccessat(0, filename, F_OK, AT_EACCESS) == 0)
    {
        return true;
    } else {
        return false;
    }
}
