#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("AcikDunyaSurus", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define PI 3.14159265358979323846f
#define CELL 80.0f

typedef struct {
    unsigned int color;
    float x, y, z;
} Vertex;

static unsigned int __attribute__((aligned(16))) list[262144];
static Vertex __attribute__((aligned(16))) cubeVerts[36];

static float px = 0.0f, pz = 0.0f;
static float heading = 0.0f, speed = 0.0f;
static float nitro = 100.0f;
static int coins = 0, collisions = 0;
static int running = 1;

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static unsigned int hash2(int x, int z) {
    unsigned int n = (unsigned int)(x * 374761393u + z * 668265263u);
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

static void initCube(void) {
    const float p[8][3] = {
        {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},
        {0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
        {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},
        {0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}
    };
    const int idx[36] = {
        0,1,2, 0,2,3, 5,4,7, 5,7,6,
        4,0,3, 4,3,7, 1,5,6, 1,6,2,
        3,2,6, 3,6,7, 4,5,1, 4,1,0
    };
    for (int i=0;i<36;i++) {
        cubeVerts[i].color = 0xffffffff;
        cubeVerts[i].x = p[idx[i]][0];
        cubeVerts[i].y = p[idx[i]][1];
        cubeVerts[i].z = p[idx[i]][2];
    }
}

static void drawCube(float x,float y,float z,float sx,float sy,float sz,
                     float ry,unsigned int color) {
    for (int i=0;i<36;i++) cubeVerts[i].color = color;
    /* Keep a separate copy in the GU command list for each draw call. */
    Vertex *drawVerts = (Vertex*)sceGuGetMemory(sizeof(cubeVerts));
    if (!drawVerts) return;
    memcpy(drawVerts, cubeVerts, sizeof(cubeVerts));
    sceGumPushMatrix();
    ScePspFVector3 pos = {x,y,z};
    ScePspFVector3 scale = {sx,sy,sz};
    sceGumTranslate(&pos);
    ScePspFVector3 rot = {0.0f,ry,0.0f};
    sceGumRotateXYZ(&rot);
    sceGumScale(&scale);
    sceGumDrawArray(GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        36, 0, drawVerts);
    sceGumPopMatrix();
}

static void setup3D(void) {
    sceGuStart(GU_DIRECT, list);
    /* PSP GU frame/depth buffers live in video memory (VRAM). */
    sceGuDrawBuffer(GU_PSM_5650, (void*)0x00000, 512);
    sceGuDispBuffer(SCR_W, SCR_H, (void*)0x88000, 512);
    sceGuDepthBuffer((void*)0x110000, 512);
    sceGuOffset(2048 - (SCR_W/2), 2048 - (SCR_H/2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFrontFace(GU_CW);
    sceGuEnable(GU_CULL_FACE);
    sceGuClearColor(0xffb8d8ee);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0,0);
    sceGuDisplay(GU_TRUE);
}

static void beginFrame(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xffb8d8ee);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(68.0f, 16.0f/9.0f, 0.5f, 650.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float sn = sinf(heading), cs = cosf(heading);
    ScePspFVector3 eye = { px + sn*10.5f, 5.2f, pz + cs*10.5f };
    ScePspFVector3 target = { px - sn*4.0f, 1.0f, pz - cs*4.0f };
    ScePspFVector3 up = {0.0f,1.0f,0.0f};
    sceGumLookAt(&eye,&target,&up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

static void renderWorld(void) {
    drawCube(px-0.0f,-0.65f,pz,900.0f,0.2f,900.0f,0.0f,0xff579447);

    int cx = (int)floorf(px/CELL);
    int cz = (int)floorf(pz/CELL);
    for (int ix=cx-4; ix<=cx+4; ix++) {
        for (int iz=cz-4; iz<=cz+4; iz++) {
            float bx = ix*CELL + CELL*0.5f;
            float bz = iz*CELL + CELL*0.5f;
            float lx = bx-px, lz = bz-pz;
            /* City blocks: asphalt cross streets and simple lane markings. */
            drawCube(bx, -0.48f, bz, CELL, 0.12f, 14.0f, 0.0f, 0xff3b3d43);
            drawCube(bx, -0.47f, bz, 14.0f, 0.13f, CELL, 0.0f, 0xff3b3d43);
            for (int m=0;m<4;m++) {
                drawCube(bx-30.0f+m*20.0f, -0.39f, bz, 5.0f,0.025f,0.35f,0.0f,0xffe8dfb2);
                drawCube(bx, -0.38f, bz-30.0f+m*20.0f, 0.35f,0.025f,5.0f,0.0f,0xffe8dfb2);
            }
            unsigned int h = hash2(ix,iz);
            float bw = 12.0f + (h % 1300)/100.0f;
            float bd = 12.0f + ((h>>7) % 1300)/100.0f;
            float bh = 10.0f + ((h>>14) % 4200)/100.0f;
            float ox = ((h>>3)%1800)/100.0f - 9.0f;
            float oz = ((h>>11)%1800)/100.0f - 9.0f;
            unsigned int palette[6] = {0xffbd8d72,0xff7598bb,0xffb6a47a,0xff9588b7,0xff80ad8c,0xffc6a2a2};
            unsigned int col = palette[(h>>22)%6];
            drawCube(bx+ox-px, bh*0.5f-0.45f, bz+oz-pz, bw,bh,bd,0.0f,col);
            /* Small roadside trees. */
            if ((h & 3u)==0) {
                drawCube(lx-23.0f,-0.02f,lz+22.0f,1.0f,2.4f,1.0f,0.0f,0xff795333);
                drawCube(lx-23.0f,1.8f,lz+22.0f,4.2f,4.2f,4.2f,0.0f,0xff28783c);
            }
        }
    }
}

static void renderCar(void) {
    /* Car is drawn in world coordinates, facing toward negative Z. */
    drawCube(px,0.05f,pz,2.05f,0.55f,4.55f,heading,0xffeeeeF2);
    drawCube(px,0.48f,pz-0.12f,1.72f,0.62f,2.15f,heading,0xff263747);
    drawCube(px,0.78f,pz-1.78f,1.75f,0.10f,0.10f,heading,0xffe9f5ff);
    drawCube(px,0.45f,pz+2.28f,1.75f,0.12f,0.08f,heading,0xffe33b35);
    for (int side=-1;side<=1;side+=2) {
        for (int front=-1;front<=1;front+=2) {
            float wx = px + side*1.03f*cosf(heading) + front*1.34f*sinf(heading);
            float wz = pz - side*1.03f*sinf(heading) + front*1.34f*cosf(heading);
            drawCube(wx,0.0f,wz,0.38f,0.82f,0.78f,heading,0xff17191d);
        }
    }
}

static void renderTraffic(void) {
    for (int i=0;i<8;i++) {
        float tx = px + (float)((i%4)-2)*10.0f + 5.0f;
        float tz = pz - 28.0f - (float)(i/4)*26.0f;
        drawCube(tx,0.0f,tz,1.9f,0.8f,4.0f,0.0f,
                 (i&1)?0xff3f76b8:0xffc69a36);
        drawCube(tx,0.48f,tz-0.1f,1.55f,0.5f,1.9f,0.0f,0xff263747);
    }
}

static void updateGame(float dt, SceCtrlData *pad) {
    int gas = (pad->Buttons & (PSP_CTRL_CROSS|PSP_CTRL_UP)) != 0;
    int brake = (pad->Buttons & (PSP_CTRL_SQUARE|PSP_CTRL_DOWN)) != 0;
    int left = (pad->Buttons & (PSP_CTRL_LEFT|PSP_CTRL_LTRIGGER)) != 0;
    int right = (pad->Buttons & (PSP_CTRL_RIGHT|PSP_CTRL_RTRIGGER)) != 0;
    int drift = (pad->Buttons & PSP_CTRL_TRIANGLE) != 0;
    int boost = (pad->Buttons & PSP_CTRL_CIRCLE) != 0 && nitro > 0.5f;

    float steer = (right?1.0f:0.0f) - (left?1.0f:0.0f);
    float maxSpeed = boost ? 62.0f : 38.0f;
    if (gas) speed += (boost?48.0f:23.0f)*dt;
    if (brake) speed -= (speed>1.0f?42.0f:15.0f)*dt;
    if (!gas && !brake) speed *= (1.0f-0.65f*dt);
    speed = clampf(speed,-12.0f,maxSpeed);
    if (boost) nitro -= 24.0f*dt;
    else nitro = fminf(100.0f,nitro+5.0f*dt);

    float turn = steer * (2.0f/(1.0f+fabsf(speed)/18.0f)) * dt;
    if (drift && fabsf(speed)>8.0f) turn *= 1.45f;
    heading += turn * (speed>=0.0f?1.0f:-1.0f);
    px += -sinf(heading)*speed*dt;
    pz += -cosf(heading)*speed*dt;

    /* Simple collision response against block edges. */
    float rx = fmodf(fabsf(px),CELL);
    float rz = fmodf(fabsf(pz),CELL);
    if (rx < 7.4f || rz < 7.4f) {
        if (fabsf(speed)>5.0f) { speed *= -0.18f; collisions++; }
        px += (rx<7.4f?2.0f:0.0f);
    }
}

int main(void) {
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceGuInit();
    initCube();
    setup3D();

    while (running) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad,1);
        if (pad.Buttons & PSP_CTRL_START) running = 0;
        float dt = 1.0f/60.0f;
        updateGame(dt,&pad);

        beginFrame();
        renderWorld();
        renderTraffic();
        renderCar();
        sceGuFinish();
        sceGuSync(0,0);
        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();

        pspDebugScreenSetXY(0,0);
        pspDebugScreenSetTextColor(0xffffffff);
        pspDebugScreenPrintf("ACIK DUNYA SURUS - PSP EDITION\n");
        pspDebugScreenPrintf("Hiz: %d km/s   Altin: %d   Carpismalar: %d\n",
                             (int)(fabsf(speed)*3.6f),coins,collisions);
        pspDebugScreenPrintf("NITRO: %d%%\n", (int)nitro);
        pspDebugScreenPrintf("X: Gaz  Kare: Fren  Yon: Direksiyon\n");
        pspDebugScreenPrintf("O: Nitro  Ucgen: Drift  START: Cikis\n");
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
