#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
using namespace std;

constexpr int                          SECTOR_SIZE                             =             512;
constexpr int                          MAX_RAID_DEVICES                        =              16;
constexpr int                          MAX_DEVICE_SECTORS                      = 1024 * 1024 * 2;
constexpr int                          MIN_DEVICE_SECTORS                      =    1 * 1024 * 2;

constexpr int                          RAID_STOPPED                            = 0;
constexpr int                          RAID_OK                                 = 1;
constexpr int                          RAID_DEGRADED                           = 2;
constexpr int                          RAID_FAILED                             = 3;

struct TBlkDev
{
    int                                  m_Devices;
    int                                  m_Sectors;

    /**
     * @brief m_Read 
     * int deviceNr
     * int sectorNr
     * const void* data
     * int sectorCnt
    */
    int                               (* m_Read )  ( int, int, void *, int );
    /**
     * @brief m_Write 
     * int deviceNr
     * int sectorNr
     * const void* data
     * int sectorCnt
    */
    int                               (* m_Write ) ( int, int, const void *, int );
};
#endif /* __PROGTEST__ */

class CRaidVolume
{
    public:
        TBlkDev m_Dev;
        int m_status = RAID_STOPPED;
        int config_size = 1;// sector
        int degraded_disk = -1;
        struct DiskState {
            int disk_index = -1;
        };

        static bool create(const TBlkDev& dev){
            char* data = new char[SECTOR_SIZE];
            bool success = true; 
            
            for(int i = 0; i < dev.m_Devices; i++){
                DiskState state;
                state.disk_index = i;
                memset(data, 0, SECTOR_SIZE);
                memcpy(data, &state, sizeof(DiskState));
                int ret = dev.m_Write(i, 0, data, 1);
                // printf("write index: %d\n", i);
                if(ret != 1) success = false;
            }
            delete [] data;
            return success;
        }
        int start(const TBlkDev& dev){
            char* data = new char[SECTOR_SIZE];
            m_Dev = dev;
            bool success = true;
            for(int i = 0; i < m_Dev.m_Devices; i++){
                if(dev.m_Read(i, 0, data, 1) != 1){
                    m_status = RAID_FAILED;
                    delete [] data;
                    return m_status;
                }
                DiskState state;
                memcpy(&state, data, sizeof(DiskState));
                if(state.disk_index != i){
                    success = false;
                    break;
                }
            }

            delete [] data;
            m_status = success ? RAID_OK: RAID_FAILED;
            return m_status;
        }


        int stop(){
            m_status = RAID_STOPPED;
            return m_status;
        }

        int resync(){
            if(m_status != RAID_DEGRADED){
                return m_status;
            }

            DiskState state;
            char* data = new char[SECTOR_SIZE];
            state.disk_index = degraded_disk;
            memcpy(data, &state, sizeof(DiskState));

            char** buffers = new char*[m_Dev.m_Devices];
            
            for (int i = 0; i < m_Dev.m_Devices; i++) {
                buffers[i] = new char[SECTOR_SIZE * m_Dev.m_Sectors-1];
            }

            memset(buffers[degraded_disk], 0, SECTOR_SIZE * m_Dev.m_Sectors-1);

            for(int j = 0; j < m_Dev.m_Devices; j++){
                if(j == degraded_disk) continue;
                int ret = m_Dev.m_Read(j, 1, buffers[j], m_Dev.m_Sectors-1);
                if(ret != m_Dev.m_Sectors-1){
                    m_status = RAID_FAILED;
                    for( int d = 0; d < m_Dev.m_Devices; d++){
                        delete [] buffers[d];
                    }
                    delete [] buffers;
                    return m_status;
                }
            }

            for(int i = 0; i < m_Dev.m_Sectors-1; i++){
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if(j == degraded_disk) continue;
                    for(int k = 0; k < SECTOR_SIZE; k++)
                        buffers[degraded_disk][i*SECTOR_SIZE + k] ^= buffers[j][i*SECTOR_SIZE + k];
                }
            }

            int ret = m_Dev.m_Write(degraded_disk, 1, buffers[degraded_disk] + SECTOR_SIZE, m_Dev.m_Sectors-1);
            if(ret != m_Dev.m_Sectors-1){
                m_status = RAID_DEGRADED;
            } else {
                degraded_disk = -1;
                m_status = RAID_OK;
            }

            for(int d = 0; d < m_Dev.m_Devices; d++){
                delete [] buffers[d];
            }

            delete [] buffers;
            return m_status;
        }

        int status() const{
            return m_status;
        }

        int size() const {
            return (m_Dev.m_Devices-1) * (m_Dev.m_Sectors-1);
        }
            // 0 buffer 1
        bool read(int secNr, void* data, int secCnt){
            int startingSector = secNr / (m_Dev.m_Devices - 1) + config_size;
            int startingDisk = secNr % (m_Dev.m_Devices - 1);
            int parity_at_start_disk = (startingSector) % m_Dev.m_Devices;
            if(startingDisk >= parity_at_start_disk){
                startingDisk++;
            }
            int endingSector = (secNr+secCnt-1) / (m_Dev.m_Devices - 1) + config_size;
            int endingDisk = (secNr+secCnt-1) % (m_Dev.m_Devices - 1);
            int parity_at_end_disk = (endingSector) % m_Dev.m_Devices;
            if(endingDisk >= parity_at_end_disk){
                endingDisk++;
            }
            int row_size = endingSector - startingSector + 1;
    
            char** buffers = new char*[m_Dev.m_Devices];
            for (int i = 0; i < m_Dev.m_Devices; i++) {
                buffers[i] = new char[SECTOR_SIZE * row_size];
            }

            for(int j = 0; j < m_Dev.m_Devices; j++){
                int ret = m_Dev.m_Read(j, startingSector, buffers[j], row_size);
                if(ret != row_size){
                    printf("read: read failed\n");
                }
            }

            int data_index = 0;
            char* dataPtr = static_cast<char*>(data);
            for(int i = 0; i < row_size; i++){
                int parity_disk = (startingSector + i) % m_Dev.m_Devices;
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if(j == parity_disk) continue;
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)) continue;
                    memcpy(dataPtr + (data_index * SECTOR_SIZE), buffers[j] + (i * SECTOR_SIZE), SECTOR_SIZE);
                    data_index++;
                }
            }
            for (int i = 0; i < m_Dev.m_Devices; i++) {
                delete [] buffers[i];
            }
            delete [] buffers;
            return true;
        }

        bool write(int secNr, const void* data, int secCnt){
            int startingSector = secNr / (m_Dev.m_Devices - 1) + config_size;
            int startingDisk = secNr % (m_Dev.m_Devices - 1);
            int parity_at_start_disk = (startingSector) % m_Dev.m_Devices;
            if(startingDisk >= parity_at_start_disk){
                startingDisk++;
            }
            int endingSector = (secNr+secCnt-1) / (m_Dev.m_Devices - 1) + config_size;
            int endingDisk = (secNr+secCnt-1) % (m_Dev.m_Devices - 1);
            int parity_at_end_disk = (endingSector) % m_Dev.m_Devices;
            if(endingDisk >= parity_at_end_disk){
                endingDisk++;
            }
            int row_size = endingSector - startingSector + 1;

            char** old_data = new char*[m_Dev.m_Devices];
            char** new_data = new char*[m_Dev.m_Devices];

            for(int i = 0; i < m_Dev.m_Devices; i++){
                old_data[i] = new char[SECTOR_SIZE * row_size];
                new_data[i] = new char[SECTOR_SIZE * row_size];
                int ret = m_Dev.m_Read(i, startingSector, old_data[i], row_size);
                if(ret != row_size){
                    printf("write: read failed\n");
                }
                memset(new_data[i], 0, SECTOR_SIZE);
            }

            const char* dataPtr = static_cast<const char*>(data);
            for(int i = 0; i < row_size; i++){
                int parity_disk = (startingSector + i) % m_Dev.m_Devices;
                
                for(int j = 0; j < m_Dev.m_Devices; j++){
                    if( j == parity_disk) continue;
                    if((i == 0 && j < startingDisk) || (i == row_size - 1 && j > endingDisk)) continue;
                    memcpy(new_data[j] + i*SECTOR_SIZE, dataPtr, SECTOR_SIZE);
                    dataPtr += SECTOR_SIZE;
                }

                for (int k = 0; k < SECTOR_SIZE; k++) {
                    char parity = 0;
                    for (int j = 0; j < m_Dev.m_Devices; j++) {
                        if (j == parity_disk) continue;
                        parity ^= new_data[j][i * SECTOR_SIZE + k];
                        parity ^= old_data[j][i * SECTOR_SIZE + k];
                    }
                    new_data[parity_disk][i * SECTOR_SIZE + k] = parity;
                }
            }

            for (int i = 0; i < m_Dev.m_Devices; i++) {
                int ret = m_Dev.m_Write(i, startingSector, new_data[i], row_size);
                if(ret != row_size){
                    printf("write: write failed\n");
                }
                delete[] old_data[i];
                delete[] new_data[i];
            }
            delete[] old_data;
            delete[] new_data;
            return true;
        }
    protected:
        // todo
};

#ifndef __PROGTEST__
// #include "tests.inc"


constexpr int                          RAID_DEVICES = 4;
constexpr int                          DISK_SECTORS = 8192;
static FILE                          * g_Fp[RAID_DEVICES];

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your Raid driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
int diskRead( int device, int sectorNr, void * data, int sectorCnt)
{
    if ( device < 0 || device >= RAID_DEVICES )
        return 0;
    if ( g_Fp[device] == nullptr )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int diskWrite  ( int device, int  sectorNr, const void * data, int sectorCnt)
{
    if ( device < 0 || device >= RAID_DEVICES )
        return 0;
    if ( g_Fp[device] == NULL )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fwrite ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** A function which releases resources allocated by openDisks/createDisks
 */
void  doneDisks ()
{
    for ( int i = 0; i < RAID_DEVICES; i ++ )
        if ( g_Fp[i] )
        {
            fclose ( g_Fp[i] );
            g_Fp[i]  = nullptr;
        }
}
//-------------------------------------------------------------------------------------------------
/** A function which creates the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev createDisks()
{
    char       buffer[SECTOR_SIZE];
    TBlkDev    res;
    char       fn[100];

    memset    ( buffer, 0, sizeof ( buffer ) );
    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
        g_Fp[i] = fopen ( fn, "w+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage create error" );
        }

        for ( int j = 0; j < DISK_SECTORS; j ++ )
            if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[i] ) != 1 )
            {
                doneDisks ();
                throw std::runtime_error ( "Raw storage create error" );
            }
    }

    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}
//-------------------------------------------------------------------------------------------------
/** A function which opens the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev                                openDisks                               ()
{
    TBlkDev    res;
    char       fn[100];

    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
        g_Fp[i] = fopen ( fn, "r+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage access error" );
        }
        fseek ( g_Fp[i], 0, SEEK_END );
        if ( ftell ( g_Fp[i] ) != DISK_SECTORS * SECTOR_SIZE )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage read error" );
        }
    }
    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}
//-------------------------------------------------------------------------------------------------
void test0 (){
    printf("initialization and finalization test\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}
void test_read (){
    printf("read one time\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    char buffer [SECTOR_SIZE];
    assert ( vol . read ( 0, buffer, 1 ) );

    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void test1(){
    printf("read value is zero\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    for ( int i = 0; i < vol.size(); i++ ){
        char buffer [SECTOR_SIZE];
        char reference_buffer[SECTOR_SIZE];
        assert ( vol . read ( i, buffer, 1 ) );
        memcpy(reference_buffer, buffer, SECTOR_SIZE);
        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE) == 0); 
    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void                                   test2                                ()
{
    printf("content correctness 1 sector\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    int size = 1;
    for( int i = 0; i < vol.size(); i++){
        // printf("%d\n", i);
        assert(vol.status() == RAID_OK);
        char buffer[SECTOR_SIZE*size];
        char reference_buffer[SECTOR_SIZE*size];

        for (int j = 0; j < SECTOR_SIZE*size; j++) {
            buffer[j] = rand() % 256;
        }

        memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
        assert(vol.write( i, buffer, size));
        assert(vol.status() == RAID_OK);

        memset(buffer, 0, SECTOR_SIZE*size);
        assert(vol.read( i, buffer, size));
        assert(vol.status() == RAID_OK);
        assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
    }
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

void test3 ()
{
    printf("content correctness n sector\n");
    TBlkDev  dev = createDisks ();
    assert ( CRaidVolume::create ( dev ) );
    CRaidVolume vol;
    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );
    for(int size = 2; size < 1000; size *= size ){
        printf("%d\n", size);
        for( int i = 0; i < 20 - (size-1); i++){
            // printf("%d\n", i);
            assert(vol.status() == RAID_OK);
            char buffer[SECTOR_SIZE*size];
            char reference_buffer[SECTOR_SIZE*size];

            for (int j = 0; j < SECTOR_SIZE*size; j++) {
                buffer[j] = rand() % 256;
            }

            memcpy(reference_buffer, buffer, SECTOR_SIZE*size);
            assert(vol.write( i, buffer, size));
            assert(vol.status() == RAID_OK);
            memset(buffer, 0, SECTOR_SIZE*size);
            assert(vol.read( i, buffer, size));
            assert(vol.status() == RAID_OK);
            assert(memcmp(buffer, reference_buffer, SECTOR_SIZE*size) == 0);
        }

    }
    
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    doneDisks ();
}

//-------------------------------------------------------------------------------------------------
void                                   test20                                  ()
{
    /* The RAID as well as disks are stopped. It corresponds i.e. to the
     * restart of a real computer.
     *
     * after the restart, we will not create the disks, nor create RAID (we do not
     * want to destroy the content). Instead, we will only open/start the devices:
     */

    TBlkDev dev = openDisks ();
    CRaidVolume vol;

    assert ( vol . start ( dev ) == RAID_OK );


    /* some I/O: RaidRead/RaidWrite
     */

    vol . stop ();
    doneDisks ();
}
//-------------------------------------------------------------------------------------------------
int                                    main                                    ()
{
    test0();
    test_read();
    test1 ();
    test2 ();
    test3();
    return EXIT_SUCCESS;
}







#endif /* __PROGTEST__ */
