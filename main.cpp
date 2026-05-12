#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "shader.h"
#include "camera.h"
#include "physics.h"
#include "day_night.h"
#include "entities.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <map>
#include <ctime>
#include <algorithm>
using namespace std;
using namespace glm;
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ======== SHADER SOURCES ========
const char* phongVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoords;
layout(location=3) in vec3 aColor;
out vec3 FragPos; out vec3 Normal; out vec2 TexCoords; out vec3 vertexColor;
uniform mat4 model, view, projection;
uniform bool windEnabled;
uniform vec3 windOffset;
void main(){
    vec3 wp = vec3(model * vec4(aPos,1.0));
    if(windEnabled) {
        float h = max(aPos.y, 0.0) * 0.015;
        wp.x += windOffset.x * h * sin(wp.x * 0.5 + wp.z * 0.3);
        wp.z += windOffset.z * h * cos(wp.z * 0.5 + wp.x * 0.2);
    }
    FragPos = wp;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords; vertexColor = aColor;
    gl_Position = projection * view * vec4(wp,1.0);
})";

const char* phongFS = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos, Normal, vertexColor; in vec2 TexCoords;
struct Material { vec3 ambient, diffuse, specular, emissive; float shininess; };
struct PL { vec3 position, ambient, diffuse, specular; float constant, linear, quadratic; };
struct DL { vec3 direction, ambient, diffuse, specular; };
struct SL { vec3 position, direction, ambient, diffuse, specular; float constant, linear, quadratic, cutOff, outerCutOff; };
uniform Material mat1;
uniform PL pointLights[6]; uniform DL dirLight; uniform SL spotLight;
uniform int numPointLights; uniform bool lightOn[6]; uniform bool dirLightOn; uniform bool spotLightOn;
uniform vec3 viewPos; uniform sampler2D tex1;
uniform int texMode; uniform bool allOff; uniform float ambStr;
uniform float timeVal;
vec3 calcPL(PL l, vec3 n, vec3 fp, vec3 vd, vec3 bc){
    vec3 ld=normalize(l.position-fp); float d=length(l.position-fp);
    float att=1.0/(l.constant+l.linear*d+l.quadratic*d*d);
    vec3 a=l.ambient*mat1.ambient*bc;
    float df=max(dot(n,ld),0.0); vec3 dif=l.diffuse*df*mat1.diffuse*bc;
    vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
    vec3 sp=l.specular*s*mat1.specular;
    return (a+dif+sp)*att;
}
vec3 calcDL(DL l, vec3 n, vec3 vd, vec3 bc){
    vec3 ld=normalize(-l.direction);
    vec3 a=l.ambient*mat1.ambient*bc;
    float df=max(dot(n,ld),0.0); vec3 dif=l.diffuse*df*mat1.diffuse*bc;
    vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
    vec3 sp=l.specular*s*mat1.specular;
    return a+dif+sp;
}
vec3 calcSL(SL l, vec3 n, vec3 fp, vec3 vd, vec3 bc){
    vec3 ld=normalize(l.position-fp);
    float theta=dot(ld,normalize(-l.direction));
    if(theta<l.cutOff) return vec3(0.0);
    float epsilon=l.cutOff-l.outerCutOff;
    float intensity=clamp((theta-l.outerCutOff)/epsilon,0.0,1.0);
    float d=length(l.position-fp);
    float att=1.0/(l.constant+l.linear*d+l.quadratic*d*d);
    vec3 a=l.ambient*mat1.ambient*bc;
    float df=max(dot(n,ld),0.0); vec3 dif=l.diffuse*df*mat1.diffuse*bc;
    vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
    vec3 sp=l.specular*s*mat1.specular;
    return (a+dif+sp)*att*intensity;
}
void main(){
    if(texMode==4) { FragColor=vec4(texture(tex1,TexCoords).rgb + mat1.emissive, 1.0); return; }
    if(texMode==5) { vec2 wuv=TexCoords; wuv.x+=sin(timeVal*0.5+TexCoords.y*4.0)*0.02; wuv.y+=timeVal*0.015; vec3 wc=texture(tex1,wuv).rgb; FragColor=vec4(wc,1.0); return; }
    // Shader Illumination Formula:
    // I = (Ambient + Diffuse + Specular) * attenuation + Emissive
    // Normal / Bump mapping formula (Grayscale approximation):
    // N' = N - T * (dtex / dx) - B * (dtex / dy)
    vec3 n=normalize(Normal);
    if((texMode==0 || texMode==1) && abs(n.y) < 0.99) {
        vec2 off = vec2(0.015, 0.0);
        float v0 = texture(tex1, TexCoords).r;
        float vx = texture(tex1, TexCoords + off.xy).r;
        float vy = texture(tex1, TexCoords + off.yx).r;
        vec3 T = normalize(vec3(-n.z, 0.0, n.x));
        vec3 B = normalize(cross(n, T));
        n = normalize(n - T*(vx-v0)*2.0 - B*(vy-v0)*2.0);
    }
    vec3 vd=normalize(viewPos-FragPos), bc;
    if(texMode==0) bc=texture(tex1,TexCoords).rgb;
    else if(texMode==1) bc=texture(tex1,TexCoords).rgb*mat1.diffuse;
    else if(texMode==2) bc=vertexColor;
    else bc=mat1.diffuse;
    if(allOff){FragColor=vec4(bc*ambStr,1.0);return;}
    vec3 r=vec3(0.0);
    if(dirLightOn) r+=calcDL(dirLight,n,vd,bc);
    if(spotLightOn) r+=calcSL(spotLight,n,FragPos,vd,bc);
    for(int i=0;i<numPointLights;i++) if(lightOn[i]) r+=calcPL(pointLights[i],n,FragPos,vd,bc);
    r+=mat1.emissive;
    FragColor=vec4(r,1.0);
})";

const char* gouraudVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aTexCoords;
layout(location=3) in vec3 aColor;
out vec3 LitColor; out vec2 TexCoords; out vec3 vColor;
struct Material { vec3 ambient, diffuse, specular, emissive; float shininess; };
struct PL { vec3 position, ambient, diffuse, specular; float constant, linear, quadratic; };
struct DL { vec3 direction, ambient, diffuse, specular; };
struct SL { vec3 position, direction, ambient, diffuse, specular; float constant, linear, quadratic, cutOff, outerCutOff; };
uniform Material mat1; uniform PL pointLights[6]; uniform DL dirLight; uniform SL spotLight;
uniform int numPointLights; uniform bool lightOn[6]; uniform bool dirLightOn; uniform bool spotLightOn;
uniform vec3 viewPos; uniform mat4 model, view, projection;
uniform bool allOff; uniform float ambStr;
uniform bool windEnabled; uniform vec3 windOffset;
void main(){
    vec3 wp=vec3(model*vec4(aPos,1.0));
    if(windEnabled){
        float h=max(aPos.y,0.0)*0.015;
        wp.x+=windOffset.x*h*sin(wp.x*0.5+wp.z*0.3);
        wp.z+=windOffset.z*h*cos(wp.z*0.5+wp.x*0.2);
    }
    vec3 n=normalize(mat3(transpose(inverse(model)))*aNormal);
    vec3 vd=normalize(viewPos-wp);
    TexCoords=aTexCoords; vColor=aColor;
    if(allOff){LitColor=vec3(ambStr);gl_Position=projection*view*vec4(wp,1.0);return;}
    vec3 r=vec3(0.0);
    if(dirLightOn){
        vec3 ld=normalize(-dirLight.direction);
        vec3 a=dirLight.ambient*mat1.ambient;
        float df=max(dot(n,ld),0.0); vec3 dif=dirLight.diffuse*df*mat1.diffuse;
        vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
        vec3 sp=dirLight.specular*s*mat1.specular;
        r+=a+dif+sp;
    }
    if(spotLightOn){
        vec3 ld=normalize(spotLight.position-wp);
        float theta=dot(ld,normalize(-spotLight.direction));
        if(theta>spotLight.outerCutOff){
            float epsilon=spotLight.cutOff-spotLight.outerCutOff;
            float intensity=clamp((theta-spotLight.outerCutOff)/epsilon,0.0,1.0);
            float d=length(spotLight.position-wp);
            float att=1.0/(spotLight.constant+spotLight.linear*d+spotLight.quadratic*d*d);
            vec3 a=spotLight.ambient*mat1.ambient;
            float df=max(dot(n,ld),0.0); vec3 dif=spotLight.diffuse*df*mat1.diffuse;
            vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
            vec3 sp=spotLight.specular*s*mat1.specular;
            r+=(a+dif+sp)*att*intensity;
        }
    }
    for(int i=0;i<numPointLights;i++){
        if(!lightOn[i]) continue;
        vec3 ld=normalize(pointLights[i].position-wp); float d=length(pointLights[i].position-wp);
        float att=1.0/(pointLights[i].constant+pointLights[i].linear*d+pointLights[i].quadratic*d*d);
        vec3 a=pointLights[i].ambient*mat1.ambient;
        float df=max(dot(n,ld),0.0); vec3 dif=pointLights[i].diffuse*df*mat1.diffuse;
        vec3 h=normalize(ld+vd); float s=pow(max(dot(n,h),0.0),mat1.shininess);
        vec3 sp=pointLights[i].specular*s*mat1.specular;
        r+=(a+dif+sp)*att;
    }
    r+=mat1.emissive; LitColor=r;
    gl_Position=projection*view*vec4(wp,1.0);
})";

const char* gouraudFS = R"(
#version 330 core
out vec4 FragColor;
in vec3 LitColor, vColor; in vec2 TexCoords;
uniform sampler2D tex1; uniform int texMode;
uniform float timeVal;
void main(){
    if(texMode==4) { FragColor=vec4(texture(tex1,TexCoords).rgb, 1.0); return; }
    if(texMode==5) { vec2 wuv=TexCoords; wuv.x+=sin(timeVal*0.5+TexCoords.y*4.0)*0.02; wuv.y+=timeVal*0.015; vec3 wc=texture(tex1,wuv).rgb*LitColor; FragColor=vec4(wc,1.0); return; }
    vec3 bc;
    if(texMode==0) bc=texture(tex1,TexCoords).rgb;
    else if(texMode==1) bc=texture(tex1,TexCoords).rgb*LitColor;
    else if(texMode==2) bc=vColor;
    else bc=vec3(1.0);
    FragColor=vec4(bc*LitColor,1.0);
})";

// ======== UI & TEXT RENDERING ========
const char* textVS = R"(
#version 330 core
layout(location=0) in vec4 vertex; // <vec2 pos, vec2 tex>
out vec2 TexCoords;
uniform mat4 projection;
uniform mat4 model;
void main() {
    gl_Position = projection * model * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vec2(vertex.z, vertex.w);
}
)";

const char* textFS = R"(
#version 330 core
in vec2 TexCoords;
out vec4 color;
uniform sampler2D textTex;
uniform vec3 textColor;
void main() {
    float sampled = texture(textTex, TexCoords).r;
    color = vec4(textColor, sampled);
}
)";

unsigned int textVAO, textVBO, textTexId;
stbtt_bakedchar textCdata[96];

void initText() {
    const char* fontPath = "C:/Windows/Fonts/arial.ttf";
    ifstream file(fontPath, ios::binary | ios::ate);
    if (!file.is_open()) { cout << "Could not load font " << fontPath << endl; return; }
    streamsize size = file.tellg(); file.seekg(0, ios::beg);
    vector<unsigned char> buffer(size);
    if (!file.read((char*)buffer.data(), size)) return;

    unsigned char* bitmap = new unsigned char[512*512];
    memset(bitmap, 0, 512*512);
    stbtt_BakeFontBitmap(buffer.data(), 0, 32.0, bitmap, 512, 512, 32, 96, textCdata);
    
    glGenTextures(1, &textTexId);
    glBindTexture(GL_TEXTURE_2D, textTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 512, 512, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    delete[] bitmap;

    glGenVertexArrays(1, &textVAO);
    glGenBuffers(1, &textVBO);
    glBindVertexArray(textVAO);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glBindVertexArray(0);
}

void renderText(Shader& s, string text, float x, float y, float scale, vec3 color) {
    s.use();
    s.setVec3("textColor", color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textTexId);
    glBindVertexArray(textVAO);
    
    // Disable depth testing for UI Overlays
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mat4 m = mat4(1.0f);
    m = translate(m, vec3(x, y, 0.0f));
    m = glm::scale(m, vec3(scale, scale, 1.0f));
    s.setMat4("model", m);

    float cx = 0.0f, cy = 0.0f; // local cursor
    for (char c : text) {
        if (c >= 32 && c < 128) {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(textCdata, 512, 512, c - 32, &cx, &cy, &q, 1);
            float vertices[6][4] = {
                { q.x0, q.y1, q.s0, q.t1 },
                { q.x1, q.y0, q.s1, q.t0 },
                { q.x0, q.y0, q.s0, q.t0 },
                { q.x0, q.y1, q.s0, q.t1 },
                { q.x1, q.y1, q.s1, q.t1 },
                { q.x1, q.y0, q.s1, q.t0 }
            };
            glBindBuffer(GL_ARRAY_BUFFER, textVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// ======== MESH ========
struct Mesh { unsigned int VAO, VBO; int vc; };
Mesh makeMesh(const vector<float>& v) {
    Mesh m; m.vc = (int)v.size() / 11;
    glGenVertexArrays(1, &m.VAO); glGenBuffers(1, &m.VBO);
    glBindVertexArray(m.VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m.VBO);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 44, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 44, (void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 44, (void*)24); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 44, (void*)32); glEnableVertexAttribArray(3);
    glBindVertexArray(0); return m;
}
void drawMesh(const Mesh& m) { if (m.vc > 0) { glBindVertexArray(m.VAO); glDrawArrays(GL_TRIANGLES, 0, m.vc); } }

// ======== VERTEX PUSH ========
void pv(vector<float>& v, vec3 p, vec3 n, float u, float t, vec3 c) {
    v.push_back(p.x); v.push_back(p.y); v.push_back(p.z);
    v.push_back(n.x); v.push_back(n.y); v.push_back(n.z);
    v.push_back(u); v.push_back(t);
    v.push_back(c.x); v.push_back(c.y); v.push_back(c.z);
}
void addTri(vector<float>& v, vec3 a, vec3 b, vec3 c, vec3 n, vec2 ua, vec2 ub, vec2 uc, vec3 col) {
    pv(v, a, n, ua.x, ua.y, col); pv(v, b, n, ub.x, ub.y, col); pv(v, c, n, uc.x, uc.y, col);
}

// ======== BOX ========
void genBox(vector<float>& v, vec3 pos, vec3 sz, vec3 col, float tr = 1.f) {
    float x = pos.x, y = pos.y, z = pos.z, w = sz.x, h = sz.y, d = sz.z;
    float x0 = x - w / 2, x1 = x + w / 2, y0 = y, y1 = y + h, z0 = z - d / 2, z1 = z + d / 2;
    float tw = w * tr, th = h * tr, td = d * tr;
    vec3 n(0, 0, 1);
    addTri(v, { x0,y0,z1 }, { x1,y0,z1 }, { x1,y1,z1 }, n, { 0,0 }, { tw,0 }, { tw,th }, col);
    addTri(v, { x0,y0,z1 }, { x1,y1,z1 }, { x0,y1,z1 }, n, { 0,0 }, { tw,th }, { 0,th }, col);
    n = vec3(0, 0, -1);
    addTri(v, { x1,y0,z0 }, { x0,y0,z0 }, { x0,y1,z0 }, n, { 0,0 }, { tw,0 }, { tw,th }, col);
    addTri(v, { x1,y0,z0 }, { x0,y1,z0 }, { x1,y1,z0 }, n, { 0,0 }, { tw,th }, { 0,th }, col);
    n = vec3(-1, 0, 0);
    addTri(v, { x0,y0,z0 }, { x0,y0,z1 }, { x0,y1,z1 }, n, { 0,0 }, { td,0 }, { td,th }, col);
    addTri(v, { x0,y0,z0 }, { x0,y1,z1 }, { x0,y1,z0 }, n, { 0,0 }, { td,th }, { 0,th }, col);
    n = vec3(1, 0, 0);
    addTri(v, { x1,y0,z1 }, { x1,y0,z0 }, { x1,y1,z0 }, n, { 0,0 }, { td,0 }, { td,th }, col);
    addTri(v, { x1,y0,z1 }, { x1,y1,z0 }, { x1,y1,z1 }, n, { 0,0 }, { td,th }, { 0,th }, col);
    n = vec3(0, 1, 0);
    addTri(v, { x0,y1,z1 }, { x1,y1,z1 }, { x1,y1,z0 }, n, { 0,0 }, { tw,0 }, { tw,td }, col);
    addTri(v, { x0,y1,z1 }, { x1,y1,z0 }, { x0,y1,z0 }, n, { 0,0 }, { tw,td }, { 0,td }, col);
    n = vec3(0, -1, 0);
    addTri(v, { x0,y0,z0 }, { x1,y0,z0 }, { x1,y0,z1 }, n, { 0,0 }, { tw,0 }, { tw,td }, col);
    addTri(v, { x0,y0,z0 }, { x1,y0,z1 }, { x0,y0,z1 }, n, { 0,0 }, { tw,td }, { 0,td }, col);
}

// ======== HOLLOW BUILDING WALLS ========
void genBuildingWallsWithDoor(vector<float>& v, vec3 pos, vec3 sz, vec3 col, float doorX, float doorW, float doorH) {
    float th = 1.0f; // wall thickness
    // Back wall
    genBox(v, vec3(pos.x, pos.y, pos.z - sz.z/2 + th/2), vec3(sz.x, sz.y, th), col, 0.5f);
    // Left & Right side walls
    genBox(v, vec3(pos.x - sz.x/2 + th/2, pos.y, pos.z), vec3(th, sz.y, sz.z - th*2), col, 0.5f);
    genBox(v, vec3(pos.x + sz.x/2 - th/2, pos.y, pos.z), vec3(th, sz.y, sz.z - th*2), col, 0.5f);
    
    // Front wall components
    float L = pos.x - sz.x/2 + th;
    float R = pos.x + sz.x/2 - th;
    float doorLeft = doorX - doorW/2;
    float doorRight = doorX + doorW/2;
    float frontZ = pos.z + sz.z/2 - th/2;
    
    if (doorLeft > L) genBox(v, vec3((L + doorLeft)/2, pos.y, frontZ), vec3(doorLeft - L, sz.y, th), col, 0.5f);
    if (doorRight < R) genBox(v, vec3((doorRight + R)/2, pos.y, frontZ), vec3(R - doorRight, sz.y, th), col, 0.5f);
    genBox(v, vec3(doorX, pos.y + doorH, frontZ), vec3(doorW, sz.y - doorH, th), col, 0.5f);
    // Roof
    genBox(v, vec3(pos.x, pos.y + sz.y - th/2, pos.z), vec3(sz.x, th, sz.z), col, 0.5f);
}

// ======== PLANE ========
void genPlane(vector<float>& v, vec3 c, float w, float d, vec3 col, float tr = 1.f) {
    vec3 n(0, 1, 0); float x0 = c.x - w / 2, x1 = c.x + w / 2, z0 = c.z - d / 2, z1 = c.z + d / 2;
    float tw = w * tr / 4, td = d * tr / 4;
    addTri(v, { x0,c.y,z1 }, { x1,c.y,z1 }, { x1,c.y,z0 }, n, { 0,0 }, { tw,0 }, { tw,td }, col);
    addTri(v, { x0,c.y,z1 }, { x1,c.y,z0 }, { x0,c.y,z0 }, n, { 0,0 }, { tw,td }, { 0,td }, col);
}

// ======== PLANE with consistent UV (for roads) ========
void genRoadPlane(vector<float>& v, vec3 c, float w, float d, vec3 col) {
    vec3 n(0, 1, 0);
    float x0 = c.x - w / 2, x1 = c.x + w / 2, z0 = c.z - d / 2, z1 = c.z + d / 2;
    float uvScale = 0.15f;
    addTri(v, { x0,c.y,z1 }, { x1,c.y,z1 }, { x1,c.y,z0 }, n, { x0 * uvScale,z1 * uvScale }, { x1 * uvScale,z1 * uvScale }, { x1 * uvScale,z0 * uvScale }, col);
    addTri(v, { x0,c.y,z1 }, { x1,c.y,z0 }, { x0,c.y,z0 }, n, { x0 * uvScale,z1 * uvScale }, { x1 * uvScale,z0 * uvScale }, { x0 * uvScale,z0 * uvScale }, col);
}

// ======== SPHERE ========
void genSphere(vector<float>& v, vec3 c, float r, vec3 col, int st = 12, int se = 12) {
    for (int i = 0; i < st; i++) {
        float t1 = (float)i / st * (float)M_PI, t2 = (float)(i + 1) / st * (float)M_PI;
        for (int j = 0; j < se; j++) {
            float p1 = (float)j / se * 2 * (float)M_PI, p2 = (float)(j + 1) / se * 2 * (float)M_PI;
            vec3 a = c + r * vec3(sin(t1) * cos(p1), cos(t1), sin(t1) * sin(p1));
            vec3 b = c + r * vec3(sin(t1) * cos(p2), cos(t1), sin(t1) * sin(p2));
            vec3 d = c + r * vec3(sin(t2) * cos(p1), cos(t2), sin(t2) * sin(p1));
            vec3 e = c + r * vec3(sin(t2) * cos(p2), cos(t2), sin(t2) * sin(p2));
            vec3 na = normalize(a - c), nb = normalize(b - c), nd = normalize(d - c), ne = normalize(e - c);
            float u1 = (float)j / se, u2 = (float)(j + 1) / se, v1 = (float)i / st, v2 = (float)(i + 1) / st;
            pv(v, a, na, u1, v1, col); pv(v, d, nd, u1, v2, col); pv(v, b, nb, u2, v1, col);
            pv(v, b, nb, u2, v1, col); pv(v, d, nd, u1, v2, col); pv(v, e, ne, u2, v2, col);
        }
    }
}

// ======== DISC (for clock face, etc) ========
void genDisc(vector<float>& v, vec3 center, float r, vec3 col, vec3 norm = vec3(0, 0, 1), int seg = 24) {
    for (int i = 0; i < seg; i++) {
        float a1 = (float)i / seg * 2.f * (float)M_PI, a2 = (float)(i + 1) / seg * 2.f * (float)M_PI;
        vec3 p1 = center + vec3(cos(a1) * r, sin(a1) * r, 0);
        vec3 p2 = center + vec3(cos(a2) * r, sin(a2) * r, 0);
        pv(v, center, norm, 0.5f, 0.5f, col);
        pv(v, p1, norm, (cos(a1) + 1) * 0.5f, (sin(a1) + 1) * 0.5f, col);
        pv(v, p2, norm, (cos(a2) + 1) * 0.5f, (sin(a2) + 1) * 0.5f, col);
    }
}

// ======== QUAD (billboard) ========
void genQuad(vector<float>& v, float hw, float hh, vec3 col) {
    vec3 n(0, 0, 1);
    pv(v, vec3(-hw, -hh, 0), n, 0, 0, col); pv(v, vec3(hw, -hh, 0), n, 1, 0, col); pv(v, vec3(hw, hh, 0), n, 1, 1, col);
    pv(v, vec3(-hw, -hh, 0), n, 0, 0, col); pv(v, vec3(hw, hh, 0), n, 1, 1, col); pv(v, vec3(-hw, hh, 0), n, 0, 1, col);
}

// ======== RECT 2D ========
void genRect2D(vector<float>& v, float w, float h, vec3 col) {
    vec3 n(0, 0, 1);
    pv(v, vec3(0, -h / 2, 0), n, 0, 0, col); pv(v, vec3(w, -h / 2, 0), n, 1, 0, col); pv(v, vec3(w, h / 2, 0), n, 1, 1, col);
    pv(v, vec3(0, -h / 2, 0), n, 0, 0, col); pv(v, vec3(w, h / 2, 0), n, 1, 1, col); pv(v, vec3(0, h / 2, 0), n, 0, 1, col);
}

// ======== CONE ========
void genCone(vector<float>& v, vec3 base, float r, float h, vec3 col, int se = 12) {
    vec3 apex = base + vec3(0, h, 0);
    for (int i = 0; i < se; i++) {
        float a1 = (float)i / se * 2 * (float)M_PI, a2 = (float)(i + 1) / se * 2 * (float)M_PI;
        vec3 p1 = base + vec3(cos(a1) * r, 0, sin(a1) * r);
        vec3 p2 = base + vec3(cos(a2) * r, 0, sin(a2) * r);
        vec3 n = normalize(cross(p2 - apex, p1 - apex));
        pv(v, apex, n, (float)(i + 0.5f) / se, 1, col); pv(v, p1, n, (float)i / se, 0, col); pv(v, p2, n, (float)(i + 1) / se, 0, col);
        vec3 bn(0, -1, 0);
        pv(v, base, bn, 0.5f, 0.5f, col); pv(v, p2, bn, (float)(i + 1) / se, 0, col); pv(v, p1, bn, (float)i / se, 0, col);
    }
}

// ======== CYLINDER BETWEEN TWO POINTS ========
void genCyl(vector<float>& v, vec3 s, vec3 e, float r, vec3 col, int si = 8) {
    vec3 dir = normalize(e - s);
    vec3 up = abs(dir.y) < 0.99f ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 ri = normalize(cross(dir, up)); up = normalize(cross(ri, dir));
    for (int i = 0; i < si; i++) {
        float a1 = (float)i / si * 2 * (float)M_PI, a2 = (float)(i + 1) / si * 2 * (float)M_PI;
        vec3 n1 = cos(a1) * ri + sin(a1) * up, n2 = cos(a2) * ri + sin(a2) * up;
        vec3 p1 = s + n1 * r, p2 = s + n2 * r, p3 = e + n1 * r, p4 = e + n2 * r;
        float u1 = (float)i / si, u2 = (float)(i + 1) / si;
        pv(v, p1, n1, u1, 0, col); pv(v, p2, n2, u2, 0, col); pv(v, p3, n1, u1, 1, col);
        pv(v, p2, n2, u2, 0, col); pv(v, p4, n2, u2, 1, col); pv(v, p3, n1, u1, 1, col);
    }
}

// ======== FRACTAL TREE (improved) ========
void genTree(vector<float>& trunk, vector<float>& leaf, vec3 base, vec3 dir, float len, float rad, int dep, int mx) {
    vec3 top = base + dir * len;
    genCyl(trunk, base, top, rad, vec3(0.45f, 0.25f, 0.12f));
    if (dep >= mx) {
        // Improved leaf clusters - multiple layered triangles per cluster
        for (int i = 0; i < 10; i++) {
            float a = (float)i / 10 * 2.f * (float)M_PI, sz = len * 0.65f;
            vec3 off(cos(a) * sz * 0.5f, sz * 0.3f, sin(a) * sz * 0.5f);
            vec3 lc = top + off;
            float gr = 0.2f + 0.4f * (float)(rand() % 100) / 100.0f;
            vec3 gc(0.08f + (rand() % 10) * 0.01f, 0.35f + gr, 0.03f + (rand() % 5) * 0.01f);
            vec3 n(0, 1, 0);
            // Layer 1
            pv(leaf, lc, n, 0, 0, gc); pv(leaf, lc + vec3(sz * 0.3f, sz * 0.4f, 0), n, 1, 0, gc); pv(leaf, lc + vec3(0, sz * 0.5f, sz * 0.3f), n, 0.5f, 1, gc);
            // Layer 2
            vec3 gc2 = gc * 0.85f;
            pv(leaf, lc + vec3(0, 0.1f, 0), n, 0, 0, gc2); pv(leaf, lc + vec3(-sz * 0.25f, sz * 0.35f, sz * 0.15f), n, 1, 0, gc2); pv(leaf, lc + vec3(sz * 0.15f, sz * 0.45f, -sz * 0.25f), n, 0.5f, 1, gc2);
            // Layer 3
            vec3 gc3 = gc * 1.1f;
            pv(leaf, lc - vec3(0, -0.05f, 0), n, 0, 0, gc3); pv(leaf, lc + vec3(sz * 0.2f, sz * 0.3f, -sz * 0.2f), n, 1, 0, gc3); pv(leaf, lc + vec3(-sz * 0.2f, sz * 0.4f, sz * 0.2f), n, 0.5f, 1, gc3);
        }
        return;
    }
    float nl = len * 0.7f, nr = rad * 0.6f;
    float randA = ((rand() % 100) / 100.0f - 0.5f) * 0.1f;
    vec3 d1 = normalize(dir + vec3(0.35f + randA, 0.0f, 0.15f));
    vec3 d2 = normalize(dir + vec3(-0.35f + randA, 0.0f, -0.15f));
    vec3 d3 = normalize(dir + vec3(0.1f, 0.0f, 0.35f + randA));
    genTree(trunk, leaf, top, d1, nl, nr, dep + 1, mx);
    genTree(trunk, leaf, top, d2, nl, nr, dep + 1, mx);
    genTree(trunk, leaf, top, d3, nl, nr, dep + 1, mx);
}

// ======== BEZIER ========
// Mathematical Formula for Cubic Bezier Curve:
// B(t) = (1-t)^3 * P0 + 3(1-t)^2 * t * P1 + 3(1-t) * t^2 * P2 + t^3 * P3
// where t is in range [0, 1].
// This provides a smooth parametric curve interpolating the endpoints P0 and P3
// and being pulled towards P1 and P2 based on Bernstein polynomials.
vec3 bezPt(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float u = 1 - t;
    return u * u * u * p0 + 3.f * u * u * t * p1 + 3.f * u * t * t * p2 + t * t * t * p3;
}
void genBezierTube(vector<float>& v, vec3 p0, vec3 p1, vec3 p2, vec3 p3, float r, vec3 col, int seg = 30, int si = 8) {
    for (int i = 0; i < seg; i++) {
        float t1 = (float)i / seg, t2 = (float)(i + 1) / seg;
        vec3 a = bezPt(p0, p1, p2, p3, t1), b = bezPt(p0, p1, p2, p3, t2);
        genCyl(v, a, b, r, col, si);
    }
}

// ======== B-SPLINE ========
// Mathematical Formula for Cubic B-Spline Basis Functions:
// N_{0,3}(t) = (1-t)^3 / 6
// N_{1,3}(t) = (3t^3 - 6t^2 + 4) / 6
// N_{2,3}(t) = (-3t^3 + 3t^2 + 3t + 1) / 6
// N_{3,3}(t) = t^3 / 6
// The evaluated point is P(t) = N_{0,3}(t)*P_0 + N_{1,3}(t)*P_1 + N_{2,3}(t)*P_2 + N_{3,3}(t)*P_3
// B-Splines provide $C^2$ continuity for smoother transitions between segments.
vec3 bsplinePt(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    float b0 = (1 - t) * (1 - t) * (1 - t) / 6.0f;
    float b1 = (3 * t3 - 6 * t2 + 4) / 6.0f;
    float b2 = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6.0f;
    float b3 = t3 / 6.0f;
    return b0 * p0 + b1 * p1 + b2 * p2 + b3 * p3;
}
void genBSplineTube(vector<float>& v, const vector<vec3>& pts, float r, vec3 col, int sps = 12) {
    for (int i = 0; i < (int)pts.size() - 3; i++) {
        for (int j = 0; j < sps; j++) {
            float t1 = (float)j / sps, t2 = (float)(j + 1) / sps;
            vec3 a = bsplinePt(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], t1);
            vec3 b = bsplinePt(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], t2);
            genCyl(v, a, b, r, col, 6);
        }
    }
}

// ======== CATMULL-ROM SPLINE ========
vec3 cmr(vec3 p0, vec3 p1, vec3 p2, vec3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (-p0 + p2) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 + (-p0 + 3.f * p1 - 3.f * p2 + p3) * t3);
}
void genSplineRibbon(vector<float>& v, const vector<vec3>& pts, float w, vec3 col, int sps = 15) {
    for (int i = 0; i < (int)pts.size() - 3; i++) {
        for (int j = 0; j < sps; j++) {
            float t1 = (float)j / sps, t2 = (float)(j + 1) / sps;
            vec3 a = cmr(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], t1);
            vec3 b = cmr(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], t2);
            vec3 dir = normalize(b - a);
            vec3 right = normalize(cross(dir, vec3(0, 1, 0))) * w;
            vec3 n(0, 1, 0);
            pv(v, a - right, n, 0, t1, col); pv(v, a + right, n, 1, t1, col); pv(v, b + right, n, 1, t2, col);
            pv(v, a - right, n, 0, t1, col); pv(v, b + right, n, 1, t2, col); pv(v, b - right, n, 0, t2, col);
        }
    }
}

// ======== RULED SURFACE ========
void genRuled(vector<float>& v, const vector<vec3>& c1, const vector<vec3>& c2, vec3 col) {
    int n = (int)c1.size() - 1;
    for (int i = 0; i < n; i++) {
        vec3 a = c1[i], b = c1[i + 1], c = c2[i], d = c2[i + 1];
        vec3 nm = normalize(cross(b - a, c - a));
        float u1 = (float)i / n, u2 = (float)(i + 1) / n;
        pv(v, a, nm, u1, 0, col); pv(v, b, nm, u2, 0, col); pv(v, d, nm, u2, 1, col);
        pv(v, a, nm, u1, 0, col); pv(v, d, nm, u2, 1, col); pv(v, c, nm, u1, 1, col);
    }
}

// ======== SHAHID MINAR ========
void genShahidMinar(vector<float>& v) {
    genBox(v, vec3(-40, 0, 10), vec3(20, 0.8f, 14), vec3(0.6f, 0.2f, 0.15f), 1);
    genBox(v, vec3(-40, 0.8f, 10), vec3(16, 0.5f, 10), vec3(0.65f, 0.2f, 0.15f), 1);
    genBox(v, vec3(-40, 1.3f, 10), vec3(12, 0.4f, 7), vec3(0.7f, 0.2f, 0.15f), 1);
    float pillarHeights[] = { 6.0f, 8.0f, 8.5f, 8.0f, 6.0f };
    float pillarPosX[] = { -44.0f, -42.5f, -40.0f, -37.5f, -36.0f };
    for (int i = 0; i < 5; i++) {
        genCyl(v, vec3(pillarPosX[i], 1.7f, 10), vec3(pillarPosX[i], 1.7f + pillarHeights[i], 10), 0.35f, vec3(0.95f));
        genCyl(v, vec3(pillarPosX[i], 1.7f + pillarHeights[i] - 0.2f, 10), vec3(pillarPosX[i], 1.7f + pillarHeights[i], 10), 0.45f, vec3(0.8f));
    }
    genCyl(v, vec3(-40, 1.7f, 10), vec3(-40, 1.7f + 10.0f, 10), 0.45f, vec3(0.98f));
    genBox(v, vec3(-40, 11.0f, 10), vec3(8.5f, 0.4f, 0.8f), vec3(0.95f), 1);
    genBox(v, vec3(-40, 11.0f, 10), vec3(0.8f, 0.4f, 3.5f), vec3(0.95f), 1);
    for (int i = 0; i < 3; i++)
        genSphere(v, vec3(-40 + i * 0.5f, 11.2f, 10), 0.6f - i * 0.1f, vec3(0.95f), 10, 10);
    for (int side = -1; side <= 1; side += 2) {
        vec3 start = vec3(-40 + side * 2.5f, 1.7f, 10), end = vec3(-40 + side * 3.5f, 5.0f, 10);
        genCyl(v, start, end, 0.25f, vec3(0.92f));
    }
}

// ======== DURBAR BANGLA SCULPTURE ========
void genDurbarBangla(vector<float>& v) {
    // Multi-tiered pedestal
    genBox(v, vec3(45, 0, -50), vec3(8, 0.6f, 8), vec3(0.6f, 0.55f, 0.5f), 1);
    genBox(v, vec3(45, 0.6f, -50), vec3(6, 0.5f, 6), vec3(0.65f, 0.6f, 0.55f), 1);
    genBox(v, vec3(45, 1.1f, -50), vec3(5, 0.4f, 5), vec3(0.7f, 0.65f, 0.6f), 1);

    // Three liberation fighters arranged 120 degrees apart
    for (int f = 0; f < 3; f++) {
        float angle = f * 120.0f * (float)M_PI / 180.0f;
        float fx = 45 + cos(angle) * 1.5f;
        float fz = -50 + sin(angle) * 1.5f;
        vec3 figCol(0.55f, 0.5f, 0.45f);

        // Legs (2 cylinders)
        genCyl(v, vec3(fx - 0.2f, 1.5f, fz), vec3(fx - 0.15f, 3.0f, fz), 0.12f, figCol);
        genCyl(v, vec3(fx + 0.2f, 1.5f, fz), vec3(fx + 0.15f, 3.0f, fz), 0.12f, figCol);

        // Torso
        genBox(v, vec3(fx, 3.0f, fz), vec3(0.5f, 1.2f, 0.3f), figCol, 1);

        // Head
        genSphere(v, vec3(fx, 4.5f, fz), 0.22f, vec3(0.6f, 0.55f, 0.5f), 8, 8);

        // Arms - raised outward
        float armDirX = cos(angle) * 0.6f;
        float armDirZ = sin(angle) * 0.6f;
        // Right arm (raised with weapon)
        genCyl(v, vec3(fx + 0.3f, 3.8f, fz), vec3(fx + 0.3f + armDirX * 0.5f, 4.5f, fz + armDirZ * 0.5f), 0.08f, figCol);
        // Weapon (rifle - thin cylinder)
        genCyl(v, vec3(fx + 0.3f + armDirX * 0.5f, 4.3f, fz + armDirZ * 0.5f),
            vec3(fx + 0.3f + armDirX * 0.8f, 5.2f, fz + armDirZ * 0.8f), 0.03f, vec3(0.3f));
        // Left arm
        genCyl(v, vec3(fx - 0.3f, 3.8f, fz), vec3(fx - 0.3f - armDirX * 0.3f, 3.5f, fz - armDirZ * 0.3f), 0.08f, figCol);
    }

    // Central plaque
    genBox(v, vec3(45, 1.5f, -50), vec3(1.5f, 0.1f, 1.5f), vec3(0.4f, 0.35f, 0.3f), 1);
}

// ======== KUET HOOD ========
void genKUETHood(vector<float>& v, vec3 pos) {
    genBox(v, pos, vec3(4.0f, 0.3f, 4.0f), vec3(0.85f, 0.7f, 0.4f), 1);
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2)
            genCyl(v, vec3(pos.x + i * 1.5f, pos.y + 0.3f, pos.z + j * 1.5f),
                vec3(pos.x + i * 1.5f, pos.y + 2.5f, pos.z + j * 1.5f), 0.2f, vec3(0.9f, 0.85f, 0.7f));
    genBox(v, vec3(pos.x, pos.y + 2.5f, pos.z), vec3(3.5f, 0.2f, 3.5f), vec3(0.9f, 0.8f, 0.5f), 1);
    genSphere(v, vec3(pos.x, pos.y + 2.7f, pos.z), 0.5f, vec3(0.95f, 0.85f, 0.3f), 10, 10);
}

// ======== BUILDING INTERIORS (improved - inspired by interior.png) ========
void genBuildingInterior(vector<float>& v, vec3 bp, vec3 bs, vec3 lightCol) {
    float x = bp.x, y = bp.y, z = bp.z;
    float w = bs.x, h = bs.y, d = bs.z;

    // Floor (wood colored)
    genBox(v, vec3(x, y + 0.05f, z), vec3(w - 0.5f, 0.1f, d - 0.5f), vec3(0.7f, 0.55f, 0.35f), 1);

    // Ceiling lights (elongated boxes - modern style)
    for (int i = -1; i <= 1; i++) {
        genBox(v, vec3(x + i * (w / 3), y + h - 0.4f, z), vec3(0.3f, 0.1f, d * 0.6f), lightCol, 1);
    }

    // Tables (hierarchical: top + 4 legs)
    for (int ti = -1; ti <= 1; ti += 2) {
        float tx = x + ti * (w / 4);
        // Table top
        genBox(v, vec3(tx, y + 1.5f, z), vec3(2.0f, 0.1f, 1.2f), vec3(0.75f, 0.6f, 0.4f), 1);
        // 4 Legs
        for (int li = -1; li <= 1; li += 2)
            for (int lj = -1; lj <= 1; lj += 2)
                genCyl(v, vec3(tx + li * 0.8f, y + 0.1f, z + lj * 0.5f), vec3(tx + li * 0.8f, y + 1.5f, z + lj * 0.5f), 0.05f, vec3(0.5f, 0.4f, 0.3f));
    }

    // Chairs around each table
    for (int ti = -1; ti <= 1; ti += 2) {
        float tx = x + ti * (w / 4);
        for (int ci = -1; ci <= 1; ci += 2) {
            float cz = z + ci * 1.2f;
            // Seat
            genBox(v, vec3(tx, y + 1.0f, cz), vec3(0.5f, 0.05f, 0.5f), vec3(0.55f, 0.55f, 0.6f), 1);
            // Back
            genBox(v, vec3(tx, y + 1.05f, cz + ci * 0.25f), vec3(0.5f, 0.6f, 0.05f), vec3(0.55f, 0.55f, 0.6f), 1);
            // Legs
            for (int al = -1; al <= 1; al += 2)
                genCyl(v, vec3(tx + al * 0.2f, y + 0.1f, cz), vec3(tx + al * 0.2f, y + 1.0f, cz), 0.03f, vec3(0.3f));
        }
    }

    // Shelf/divider panel (dark, modern)
    genBox(v, vec3(x - w / 2 + 0.8f, y + 0.1f, z), vec3(0.15f, h * 0.5f, d * 0.4f), vec3(0.2f, 0.2f, 0.22f), 1);

    // Interior pillars
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2)
            genCyl(v, vec3(x + i * (w / 3), y, z + j * (d / 3)), vec3(x + i * (w / 3), y + h - 0.5f, z + j * (d / 3)), 0.15f, vec3(0.8f));
}

// ======== SPORTS - CRICKET PITCH & STUMPS ========
void genCricketPitch(vector<float>& v) {
    // Pitch strip (brownish)
    genBox(v, vec3(-30, 0.02f, 30), vec3(2.5f, 0.02f, 10), vec3(0.7f, 0.55f, 0.35f), 1);
    // Stumps (wickets) - two sets at each end
    for (int end = -1; end <= 1; end += 2) {
        float sz = 30 + end * 4.5f;
        for (int s = -1; s <= 1; s++) {
            genCyl(v, vec3(-30 + s * 0.12f, 0.02f, sz), vec3(-30 + s * 0.12f, 0.9f, sz), 0.02f, vec3(0.9f, 0.85f, 0.7f));
        }
        // Bails
        genBox(v, vec3(-30, 0.88f, sz), vec3(0.35f, 0.04f, 0.04f), vec3(0.85f, 0.8f, 0.6f), 1);
    }
    // Bat (leaning)
    genBox(v, vec3(-29, 0.02f, 30), vec3(0.12f, 0.8f, 0.08f), vec3(0.8f, 0.7f, 0.5f), 1);
}

// ======== SPORTS - FOOTBALL GOAL POSTS ========
void genGoalPosts(vector<float>& v) {
    for (int end = -1; end <= 1; end += 2) {
        float fz = 30 + end * 14.0f;
        // Two uprights
        genCyl(v, vec3(-33, 0, fz), vec3(-33, 3.0f, fz), 0.06f, vec3(0.9f));
        genCyl(v, vec3(-27, 0, fz), vec3(-27, 3.0f, fz), 0.06f, vec3(0.9f));
        // Crossbar
        genCyl(v, vec3(-33, 3.0f, fz), vec3(-27, 3.0f, fz), 0.06f, vec3(0.9f));
    }
}

// ======== FOOTBALL ========
void genFootball(vector<float>& v, vec3 pos) {
    // Black and white pattern approximation
    genSphere(v, pos, 0.2f, vec3(0.95f), 8, 8);
    // Add some dark patches
    for (int i = 0; i < 4; i++) {
        float a = i * 1.57f;
        vec3 pp = pos + vec3(cos(a) * 0.15f, 0.1f, sin(a) * 0.15f);
        genSphere(v, pp, 0.06f, vec3(0.15f), 4, 4);
    }
}

// ======== STUDENT FIGURE ========
void genStudentFigure(vector<float>& v) {
    // Centered at origin, will be transformed per-student
    // Legs
    genCyl(v, vec3(-0.1f, 0, 0), vec3(-0.1f, 0.6f, 0), 0.06f, vec3(0.25f, 0.25f, 0.35f));
    genCyl(v, vec3(0.1f, 0, 0), vec3(0.1f, 0.6f, 0), 0.06f, vec3(0.25f, 0.25f, 0.35f));
    // Torso
    genBox(v, vec3(0, 0.6f, 0), vec3(0.35f, 0.5f, 0.2f), vec3(0.3f, 0.5f, 0.7f), 1);
    // Head
    genSphere(v, vec3(0, 1.3f, 0), 0.13f, vec3(0.75f, 0.6f, 0.45f), 8, 8);
    // Backpack
    genBox(v, vec3(0, 0.7f, -0.15f), vec3(0.25f, 0.35f, 0.15f), vec3(0.3f, 0.25f, 0.2f), 1);
    // Arms
    genCyl(v, vec3(-0.22f, 0.9f, 0), vec3(-0.22f, 0.55f, 0.05f), 0.04f, vec3(0.75f, 0.6f, 0.45f));
    genCyl(v, vec3(0.22f, 0.9f, 0), vec3(0.22f, 0.55f, 0.05f), 0.04f, vec3(0.75f, 0.6f, 0.45f));
}

// ======== BIRD FIGURE ========
void genBirdBody(vector<float>& v) {
    // Body ellipsoid approximation
    genSphere(v, vec3(0, 0, 0), 0.15f, vec3(0.35f, 0.3f, 0.3f), 6, 6);
    // Head
    genSphere(v, vec3(0.2f, 0.05f, 0), 0.08f, vec3(0.3f, 0.28f, 0.28f), 6, 6);
    // Beak
    genCone(v, vec3(0.28f, 0.03f, 0), 0.02f, 0.08f, vec3(0.8f, 0.6f, 0.2f), 4);
    // Tail
    vec3 n(0, 1, 0);
    pv(v, vec3(-0.15f, 0, 0), n, 0, 0, vec3(0.3f)); pv(v, vec3(-0.3f, 0.05f, -0.05f), n, 1, 0, vec3(0.25f)); pv(v, vec3(-0.3f, 0.05f, 0.05f), n, 0.5f, 1, vec3(0.25f));
}

void genBirdWing(vector<float>& v) {
    vec3 n(0, 1, 0), col(0.4f, 0.35f, 0.3f);
    pv(v, vec3(0, 0, 0), n, 0, 0, col); pv(v, vec3(-0.1f, 0, 0.3f), n, 1, 0, col); pv(v, vec3(0.15f, 0, 0.15f), n, 0.5f, 1, col);
    pv(v, vec3(0, 0, 0), n, 0, 0, col); pv(v, vec3(-0.1f, 0, -0.3f), n, 1, 0, col * 0.9f); pv(v, vec3(0.15f, 0, -0.15f), n, 0.5f, 1, col * 0.9f);
}

// ======== HORIZONTAL DISC (for ceiling fans) ========
void genDiscHoriz(vector<float>& v, vec3 center, float r, vec3 col, int seg = 16) {
    vec3 n(0, -1, 0);
    for (int i = 0; i < seg; i++) {
        float a1 = (float)i / seg * 2.f * (float)M_PI, a2 = (float)(i + 1) / seg * 2.f * (float)M_PI;
        vec3 p1 = center + vec3(cos(a1) * r, 0, sin(a1) * r);
        vec3 p2 = center + vec3(cos(a2) * r, 0, sin(a2) * r);
        pv(v, center, n, 0.5f, 0.5f, col);
        pv(v, p1, n, (cos(a1) + 1) * 0.5f, (sin(a1) + 1) * 0.5f, col);
        pv(v, p2, n, (cos(a2) + 1) * 0.5f, (sin(a2) + 1) * 0.5f, col);
    }
}

// ======== STAIRCASE ========
void genSteps(vector<float>& v, vec3 pos, float w, float l, float h, vec3 col, int sc = 10) {
    float sw = l / sc, sh = h / sc;
    for (int i = 0; i < sc; i++)
        genBox(v, vec3(pos.x, pos.y + i * sh, pos.z + l / 2 - sw / 2 - i * sw), vec3(w, sh, sw), col, 1);
}

void genLabel(vector<float>& v, vec3 pos, float angleY, float w, float h) {
    // Post
    genCyl(v, vec3(pos.x, pos.y, pos.z), vec3(pos.x, pos.y + 1.2f, pos.z), 0.05f, vec3(0.2f));
    // Board (rotated abstractly here by just drawing a flat board on XZ then rotating via model matrix later, but we use genBox)
    vec3 bl = pos + vec3(0, 1.3f, 0);
    genBox(v, bl, vec3(w, h, 0.05f), vec3(0.5f, 0.4f, 0.3f), 4);
}

// ======== CLASSROOM CONTENTS ========
void genClassroom(vector<float>& v, vec3 pos, float w, float d, float h) {
    // Floor
    genBox(v, vec3(pos.x, pos.y + 0.02f, pos.z), vec3(w - 0.1f, 0.04f, d - 0.1f), vec3(0.7f, 0.55f, 0.35f), 1);
    // Blackboard
    genBox(v, vec3(pos.x, pos.y + h * 0.4f, pos.z + d / 2 - 0.1f), vec3(w * 0.6f, h * 0.25f, 0.08f), vec3(0.12f, 0.25f, 0.12f), 1);
    // Bench-desk rows (3 rows x 2 columns)
    for (int r = 0; r < 3; r++) for (int c = -1; c <= 1; c += 2) {
        float bx = pos.x + c * w * 0.2f, bz = pos.z - d * 0.15f + r * d * 0.25f;
        genBox(v, vec3(bx, pos.y + 1.1f, bz), vec3(w * 0.2f, 0.06f, 0.5f), vec3(0.6f, 0.45f, 0.28f), 1);
        genBox(v, vec3(bx, pos.y + 0.6f, bz - 0.4f), vec3(w * 0.18f, 0.04f, 0.35f), vec3(0.55f, 0.4f, 0.25f), 1);
    }
    // Teacher desk + monitor
    genBox(v, vec3(pos.x, pos.y + 1.2f, pos.z + d / 2 - 1.5f), vec3(1.4f, 0.07f, 0.7f), vec3(0.5f, 0.38f, 0.22f), 1);
    genBox(v, vec3(pos.x, pos.y + 1.55f, pos.z + d / 2 - 1.5f), vec3(0.5f, 0.35f, 0.04f), vec3(0.08f, 0.08f, 0.1f), 1);
    genCyl(v, vec3(pos.x, pos.y + 1.27f, pos.z + d / 2 - 1.5f), vec3(pos.x, pos.y + 1.55f, pos.z + d / 2 - 1.5f), 0.04f, vec3(0.3f));
    
    // Gaussian Screen Glow
    float sx = pos.x, sy = pos.y + 1.55f + 0.175f, sz = pos.z + d/2 - 1.5f - 0.021f; 
    vec3 nG(0, 0, -1);
    float gw = 0.48f, gh = 0.33f;
    for(int i=0; i<10; i++) {
        for(int j=0; j<10; j++) {
            float u1 = (float)i/10, u2 = (float)(i+1)/10;
            float v1 = (float)j/10, v2 = (float)(j+1)/10;
            float cx1 = u1-0.5f, cy1 = v1-0.5f; vec3 c1(0.1f, 0.6f * exp(-(cx1*cx1+cy1*cy1)*12), 0.8f * exp(-(cx1*cx1+cy1*cy1)*15));
            float cx2 = u2-0.5f, cy2 = v1-0.5f; vec3 c2(0.1f, 0.6f * exp(-(cx2*cx2+cy2*cy2)*12), 0.8f * exp(-(cx2*cx2+cy2*cy2)*15));
            float cx3 = u2-0.5f, cy3 = v2-0.5f; vec3 c3(0.1f, 0.6f * exp(-(cx3*cx3+cy3*cy3)*12), 0.8f * exp(-(cx3*cx3+cy3*cy3)*15));
            float cx4 = u1-0.5f, cy4 = v2-0.5f; vec3 c4(0.1f, 0.6f * exp(-(cx4*cx4+cy4*cy4)*12), 0.8f * exp(-(cx4*cx4+cy4*cy4)*15));
            vec3 p1(sx - gw/2 + u1*gw, sy - gh/2 + v1*gh, sz), p2(sx - gw/2 + u2*gw, sy - gh/2 + v1*gh, sz);
            vec3 p3(sx - gw/2 + u2*gw, sy - gh/2 + v2*gh, sz), p4(sx - gw/2 + u1*gw, sy - gh/2 + v2*gh, sz);
            pv(v, p1, nG, u1, v1, c1); pv(v, p2, nG, u2, v1, c2); pv(v, p3, nG, u2, v2, c3);
            pv(v, p1, nG, u1, v1, c1); pv(v, p3, nG, u2, v2, c3); pv(v, p4, nG, u1, v2, c4);
        }
    }
    // Fan (rod + blades)
    genCyl(v, vec3(pos.x, pos.y + h - 0.5f, pos.z), vec3(pos.x, pos.y + h - 0.05f, pos.z), 0.03f, vec3(0.4f));
    for (int b = 0; b < 4; b++) {
        float ba = b * (float)M_PI / 2.0f;
        genBox(v, vec3(pos.x + cos(ba) * 0.35f, pos.y + h - 0.5f, pos.z + sin(ba) * 0.35f), vec3(0.5f, 0.02f, 0.08f), vec3(0.6f, 0.55f, 0.5f), 1);
    }
    // Light fixture
    genBox(v, vec3(pos.x + w * 0.25f, pos.y + h - 0.1f, pos.z), vec3(0.1f, 0.04f, d * 0.5f), vec3(1.0f, 0.97f, 0.85f), 1);
    // AC unit
    genBox(v, vec3(pos.x + w / 2 - 0.15f, pos.y + h * 0.7f, pos.z), vec3(0.12f, 0.35f, 0.7f), vec3(0.88f, 0.88f, 0.92f), 1);
}

// ======== FUNCTION/MEETING ROOM ========
void genFunctionRoom(vector<float>& v, vec3 pos, float w, float d, float h) {
    // Large central meeting table
    genBox(v, vec3(pos.x, pos.y + 1.2f, pos.z), vec3(w * 0.6f, 0.1f, d * 0.4f), vec3(0.45f, 0.25f, 0.15f), 1);
    // Table legs
    for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2)
        genCyl(v, vec3(pos.x + i * 1.5f, pos.y + 0.1f, pos.z + j * 0.8f), vec3(pos.x + i * 1.5f, pos.y + 1.2f, pos.z + j * 0.8f), 0.08f, vec3(0.2f));
    
    // Chairs around table
    for (int i = -2; i <= 2; i++) {
        // Top edge
        genBox(v, vec3(pos.x + i * 0.8f, pos.y + 0.8f, pos.z - d * 0.2f - 0.4f), vec3(0.5f, 0.08f, 0.5f), vec3(0.25f, 0.28f, 0.35f), 1);
        genBox(v, vec3(pos.x + i * 0.8f, pos.y + 1.3f, pos.z - d * 0.2f - 0.6f), vec3(0.5f, 0.6f, 0.08f), vec3(0.25f, 0.28f, 0.35f), 1);
        // Bottom edge
        genBox(v, vec3(pos.x + i * 0.8f, pos.y + 0.8f, pos.z + d * 0.2f + 0.4f), vec3(0.5f, 0.08f, 0.5f), vec3(0.25f, 0.28f, 0.35f), 1);
        genBox(v, vec3(pos.x + i * 0.8f, pos.y + 1.3f, pos.z + d * 0.2f + 0.6f), vec3(0.5f, 0.6f, 0.08f), vec3(0.25f, 0.28f, 0.35f), 1);
    }
    
    // Whiteboard / Projector screen
    genBox(v, vec3(pos.x - w * 0.45f + 0.1f, pos.y + h * 0.5f, pos.z), vec3(0.08f, h * 0.4f, d * 0.5f), vec3(0.95f, 0.95f, 0.95f), 1);
    
    // 2 Fans
    for (int i = -1; i <= 1; i += 2) {
        float fx = pos.x + i * w * 0.2f;
        genCyl(v, vec3(fx, pos.y + h - 0.5f, pos.z), vec3(fx, pos.y + h - 0.05f, pos.z), 0.03f, vec3(0.35f));
        for (int b = 0; b < 4; b++) {
            float ba = b * (float)M_PI / 2.0f;
            genBox(v, vec3(fx + cos(ba) * 0.4f, pos.y + h - 0.5f, pos.z + sin(ba) * 0.4f), vec3(0.6f, 0.02f, 0.08f), vec3(0.55f, 0.5f, 0.45f), 1);
        }
    }
    // Lighting fixture
    genBox(v, vec3(pos.x, pos.y + h - 0.1f, pos.z), vec3(w * 0.5f, 0.04f, 0.2f), vec3(1.0f, 0.97f, 0.85f), 1);
    // Window
    genBox(v, vec3(pos.x + w / 2 - 0.05f, pos.y + h * 0.4f, pos.z), vec3(0.05f, h * 0.35f, d * 0.4f), vec3(0.55f, 0.7f, 0.85f), 1);
}

// ======== WASHROOM ========
void genWashroom(vector<float>& v, vec3 pos, float w, float d, float h) {
    genBox(v, vec3(pos.x, pos.y + 0.02f, pos.z), vec3(w - 0.1f, 0.04f, d - 0.1f), vec3(0.85f, 0.85f, 0.88f), 1);
    // Commode
    genBox(v, vec3(pos.x - w * 0.25f, pos.y + 0.15f, pos.z + d * 0.3f), vec3(0.4f, 0.35f, 0.5f), vec3(0.92f, 0.92f, 0.95f), 1);
    genCyl(v, vec3(pos.x - w * 0.25f, pos.y + 0.5f, pos.z + d * 0.3f + 0.15f), vec3(pos.x - w * 0.25f, pos.y + 0.8f, pos.z + d * 0.3f + 0.15f), 0.05f, vec3(0.9f));
    // Lower pan (squat)
    genBox(v, vec3(pos.x + w * 0.25f, pos.y + 0.02f, pos.z + d * 0.3f), vec3(0.45f, 0.08f, 0.6f), vec3(0.88f, 0.88f, 0.9f), 1);
    // Tap + basin
    genCyl(v, vec3(pos.x, pos.y + 1.0f, pos.z - d * 0.3f), vec3(pos.x, pos.y + 1.4f, pos.z - d * 0.3f), 0.03f, vec3(0.7f, 0.7f, 0.72f));
    genBox(v, vec3(pos.x, pos.y + 0.8f, pos.z - d * 0.3f), vec3(0.5f, 0.08f, 0.35f), vec3(0.9f, 0.9f, 0.92f), 1);
}

// ======== DINING AREA ========
void genDiningArea(vector<float>& v, vec3 pos) {
    genBox(v, vec3(pos.x, pos.y + 1.1f, pos.z), vec3(2.0f, 0.08f, 1.0f), vec3(0.65f, 0.5f, 0.35f), 1);
    for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) {
        genBox(v, vec3(pos.x + i * 0.5f, pos.y + 0.7f, pos.z + j * 0.6f), vec3(0.35f, 0.04f, 0.35f), vec3(0.5f, 0.45f, 0.5f), 1);
        genCyl(v, vec3(pos.x + i * 0.5f, pos.y, pos.z + j * 0.6f), vec3(pos.x + i * 0.5f, pos.y + 0.7f, pos.z + j * 0.6f), 0.03f, vec3(0.3f));
    }
}

// ======== ACADEMIC BUILDING ========
void genAcademicBuildingInteriorFull(vector<float>& v, vec3 bp) {
    float floorH = 4.0f;
    for (int f = 0; f < 2; f++) {
        float fy = bp.y + f * floorH;
        if (f > 0) genBox(v, vec3(bp.x, fy, bp.z), vec3(12.5f, 0.15f, 6.5f), vec3(0.7f, 0.68f, 0.65f), 1);
        genClassroom(v, vec3(bp.x - 5.0f, fy + 0.2f, bp.z - 3.0f), 5.0f, 6.0f, floorH - 0.4f);
        genClassroom(v, vec3(bp.x + 5.0f, fy + 0.2f, bp.z - 3.0f), 5.0f, 6.0f, floorH - 0.4f);
    }
    genSteps(v, vec3(bp.x, 0, bp.z + 1.5f), 2.0f, 5.0f, floorH, vec3(0.7f, 0.65f, 0.6f), 12);
    genLabel(v, vec3(bp.x - 2.5f, 0, bp.z + 3.5f), 0.0f, 1.2f, 0.8f);
}

// ======== HALL BUILDING FULL INTERIOR (2 stories, 4 rooms/floor) ========
void genHallBuildingInteriorFull(vector<float>& v, vec3 bp) {
    float floorH = 4.5f;
    float rmOff[4][2] = { {-3, -57.5f}, {3, -57.5f}, {-3, -52.5f}, {3, -52.5f} };
    for (int f = 0; f < 2; f++) {
        float fy = bp.y + f * floorH;
        if (f > 0) genBox(v, vec3(bp.x, fy, bp.z), vec3(13.5f, 0.15f, 9.5f), vec3(0.7f, 0.68f, 0.65f), 1);
        for (int r = 0; r < 4; r++)
            genFunctionRoom(v, vec3(bp.x + rmOff[r][0], fy + 0.2f, rmOff[r][1]), 5.0f, 4.0f, floorH - 0.4f);
        genWashroom(v, vec3(bp.x + 5.5f, fy + 0.2f, bp.z - 2.0f), 2.0f, 2.0f, floorH - 0.4f);
        genDiningArea(v, vec3(bp.x, fy + 0.2f, bp.z + 3.5f));
    }
    genSteps(v, vec3(bp.x + 5.5f, 0, bp.z + 3.5f), 1.2f, floorH, 2.5f, vec3(0.7f, 0.65f, 0.6f), 10);
    genSteps(v, vec3(bp.x + 5.5f, floorH, bp.z + 3.5f), 1.2f, 3.0f, 2.5f, vec3(0.7f, 0.65f, 0.6f), 8);
    genLabel(v, vec3(bp.x - 3.0f, 0, bp.z + 5.0f), 0.0f, 1.2f, 0.8f);
}

// ======== BUILDING SIGN (colored plate) ========
void genBuildingSign(vector<float>& v, vec3 pos, vec3 col) {
    genBox(v, pos, vec3(3.0f, 0.8f, 0.15f), col, 1);
    genBox(v, vec3(pos.x, pos.y, pos.z + 0.1f), vec3(2.6f, 0.5f, 0.05f), col * 0.7f, 1);
}

// ======== CLOCK TOWER ON BUILDING ========
void genClockTower(vector<float>& v) {
    genBox(v, vec3(0, 9.5f, -53.0f), vec3(2.5f, 2.5f, 1.5f), vec3(0.8f, 0.7f, 0.55f), 1);
    genDisc(v, vec3(0, 10.5f, -52.2f), 1.0f, vec3(0.95f, 0.93f, 0.88f), vec3(0, 0, 1), 24);
    for (int i = 0; i < 12; i++) {
        float a = (float)i / 12 * 2.f * (float)M_PI;
        genSphere(v, vec3(cos(a) * 0.85f, sin(a) * 0.85f + 10.5f, -52.18f), 0.04f, vec3(0.2f, 0.15f, 0.1f), 4, 4);
    }
    // Frame ring
    for (int i = 0; i < 36; i++) {
        float a = (float)i / 36 * 2.f * (float)M_PI;
        genSphere(v, vec3(cos(a) * 1.05f, sin(a) * 1.05f + 10.5f, -52.15f), 0.04f, vec3(0.4f, 0.35f, 0.2f), 3, 3);
    }
}

// ======== ENHANCED LAKE ========
void genEnhancedLake(vector<float>& v, vector<float>& shore) {
    genPlane(v, vec3(26, 0.04f, -8), 24, 18, vec3(0.2f, 0.4f, 0.6f), 1);
    // Shoreline (sandy ring around water)
    float cx = 26, cz = -8, hw = 12.5f, hd = 9.5f;
    for (int side = 0; side < 4; side++) {
        vec3 sp; vec3 ss;
        if (side == 0) { sp = vec3(cx, 0.025f, cz + hd + 0.5f); ss = vec3(hw * 2 + 1, 0.03f, 1.0f); }
        else if (side == 1) { sp = vec3(cx, 0.025f, cz - hd - 0.5f); ss = vec3(hw * 2 + 1, 0.03f, 1.0f); }
        else if (side == 2) { sp = vec3(cx + hw + 0.5f, 0.025f, cz); ss = vec3(1.0f, 0.03f, hd * 2 + 1); }
        else { sp = vec3(cx - hw - 0.5f, 0.025f, cz); ss = vec3(1.0f, 0.03f, hd * 2 + 1); }
        genBox(shore, sp, ss, vec3(0.65f, 0.55f, 0.35f), 1);
    }
    // Lily pads
    for (int i = 0; i < 6; i++) {
        float lx = cx - 5 + (i % 3) * 5.0f, lz = cz - 3 + (i / 3) * 6.0f;
        genDiscHoriz(v, vec3(lx, 0.05f, lz), 0.3f + (i % 2) * 0.15f, vec3(0.15f, 0.5f, 0.15f), 8);
    }
}

// ======== FULL CRICKET FIELD ========
void genFullCricketField(vector<float>& v) {
    genBox(v, vec3(44, 0.02f, 38), vec3(2.5f, 0.02f, 10), vec3(0.7f, 0.55f, 0.35f), 1);
    // Circular boundary
    for (int i = 0; i < 32; i++) {
        float a1 = (float)i / 32 * 2.f * (float)M_PI, a2 = (float)(i + 1) / 32 * 2.f * (float)M_PI;
        genCyl(v, vec3(44 + cos(a1) * 14, 0.01f, 38 + sin(a1) * 14), vec3(44 + cos(a2) * 14, 0.01f, 38 + sin(a2) * 14), 0.03f, vec3(0.9f));
    }
    // Stumps
    for (int end = -1; end <= 1; end += 2) {
        float sz = 38 + end * 4.5f;
        for (int s = -1; s <= 1; s++)
            genCyl(v, vec3(44 + s * 0.12f, 0.02f, sz), vec3(44 + s * 0.12f, 0.9f, sz), 0.02f, vec3(0.9f, 0.85f, 0.7f));
        genBox(v, vec3(44, 0.88f, sz), vec3(0.35f, 0.04f, 0.04f), vec3(0.85f, 0.8f, 0.6f), 1);
    }
    genBox(v, vec3(45, 0.02f, 38), vec3(0.12f, 0.8f, 0.08f), vec3(0.8f, 0.7f, 0.5f), 1);
}

// ======== FULL FOOTBALL FIELD ========
void genFullFootballField(vector<float>& v) {
    // Boundary lines
    float fx = -52, fz = -28, fw = 12, fd = 18;
    for (int i = 0; i < 2; i++) {
        genBox(v, vec3(fx, 0.015f, fz + (i == 0 ? -fd / 2 : fd / 2)), vec3(fw, 0.01f, 0.08f), vec3(0.9f), 1);
        genBox(v, vec3(fx + (i == 0 ? -fw / 2 : fw / 2), 0.015f, fz), vec3(0.08f, 0.01f, fd), vec3(0.9f), 1);
    }
    // Center line
    genBox(v, vec3(fx, 0.015f, fz), vec3(fw, 0.01f, 0.06f), vec3(0.9f), 1);
    // Center circle
    for (int i = 0; i < 24; i++) {
        float a1 = (float)i / 24 * 2.f * (float)M_PI, a2 = (float)(i + 1) / 24 * 2.f * (float)M_PI;
        genCyl(v, vec3(fx + cos(a1) * 3, 0.015f, fz + sin(a1) * 3), vec3(fx + cos(a2) * 3, 0.015f, fz + sin(a2) * 3), 0.03f, vec3(0.9f));
    }
    // Goal posts
    for (int end = -1; end <= 1; end += 2) {
        float gz = fz + end * fd / 2;
        genCyl(v, vec3(fx - 3, 0, gz), vec3(fx - 3, 3.0f, gz), 0.06f, vec3(0.9f));
        genCyl(v, vec3(fx + 3, 0, gz), vec3(fx + 3, 3.0f, gz), 0.06f, vec3(0.9f));
        genCyl(v, vec3(fx - 3, 3.0f, gz), vec3(fx + 3, 3.0f, gz), 0.06f, vec3(0.9f));
    }
    // Corner flags
    float corners[4][2] = { {fx - fw / 2, fz - fd / 2}, {fx + fw / 2, fz - fd / 2}, {fx - fw / 2, fz + fd / 2}, {fx + fw / 2, fz + fd / 2} };
    for (int i = 0; i < 4; i++) {
        genCyl(v, vec3(corners[i][0], 0, corners[i][1]), vec3(corners[i][0], 1.5f, corners[i][1]), 0.02f, vec3(0.9f));
        genCone(v, vec3(corners[i][0], 1.3f, corners[i][1]), 0.15f, 0.25f, vec3(0.9f, 0.1f, 0.1f), 4);
    }
}

// ======== TRAFFIC CONE ========
void genTrafficCone(vector<float>& v, vec3 pos) {
    genCone(v, vec3(pos.x, pos.y, pos.z), 0.25f, 0.8f, vec3(0.95f, 0.4f, 0.05f), 8);
    genBox(v, vec3(pos.x, pos.y, pos.z), vec3(0.5f, 0.05f, 0.5f), vec3(0.95f, 0.4f, 0.05f), 1);
    // White stripes
    genCyl(v, vec3(pos.x - 0.1f, pos.y + 0.3f, pos.z), vec3(pos.x + 0.1f, pos.y + 0.35f, pos.z), 0.14f, vec3(0.95f), 6);
    genCyl(v, vec3(pos.x - 0.06f, pos.y + 0.55f, pos.z), vec3(pos.x + 0.06f, pos.y + 0.58f, pos.z), 0.09f, vec3(0.95f), 6);
}

// ======== BOLLARD ========
void genBollard(vector<float>& v, vec3 pos) {
    genCyl(v, pos, pos + vec3(0, 0.8f, 0), 0.06f, vec3(0.3f));
    genSphere(v, pos + vec3(0, 0.9f, 0), 0.12f, vec3(0.9f, 0.85f, 0.4f), 6, 6);
}

// ======== ROAD MARKINGS ========
void genRoadMarkings(vector<float>& v) {
    // Yellow center lines on main roads
    genBox(v, vec3(0, 0.025f, 0), vec3(0.08f, 0.005f, 118), vec3(0.9f, 0.8f, 0.2f), 1);
    genBox(v, vec3(0, 0.025f, -30), vec3(78, 0.005f, 0.08f), vec3(0.9f, 0.8f, 0.2f), 1);
    genBox(v, vec3(0, 0.025f, 20), vec3(78, 0.005f, 0.08f), vec3(0.9f, 0.8f, 0.2f), 1);
    // Keep road edges clean/dark: no white edge paint strips.
}

// ======== BEZIER EVALUATION FOR HOLLOW ========
long long nCr(int n, int r) {
    if (r > n / 2) r = n - r;
    long long ans = 1;
    for (int i = 1; i <= r; i++) { ans *= n - r + i; ans /= i; }
    return ans;
}
vec2 bezierEval2D(float t, const vector<vec3>& pts) {
    int L = (int)pts.size() - 1;
    float x = 0, y = 0;
    for (int i = 0; i <= L; i++) {
        float coef = (float)(nCr(L, i) * pow(1 - (double)t, L - i) * pow((double)t, i));
        x += coef * pts[i].x; y += coef * pts[i].y;
    }
    return vec2(x, y);
}
void genHollowBezier(vector<float>& v, const vector<vec3>& ctrlPts, vec3 col, int nt = 10, int ntheta = 20) {
    vector<vec2> profile;
    for (int i = 0; i <= nt; i++) profile.push_back(bezierEval2D((float)i / nt, ctrlPts));
    float dtheta = 2.f * (float)M_PI / ntheta;
    for (int i = 0; i < (int)profile.size() - 1; i++) for (int j = 0; j < ntheta; j++) {
        float t1 = j * dtheta, t2 = (j + 1) * dtheta;
        float r1 = profile[i].x, y1 = profile[i].y, r2 = profile[i + 1].x, y2 = profile[i + 1].y;
        vec3 a(r1 * cos(t1), y1, -r1 * sin(t1)), b(r1 * cos(t2), y1, -r1 * sin(t2));
        vec3 c(r2 * cos(t1), y2, -r2 * sin(t1)), d(r2 * cos(t2), y2, -r2 * sin(t2));
        vec3 na = normalize(vec3(cos(t1), 0, -sin(t1))), nb = normalize(vec3(cos(t2), 0, -sin(t2)));
        float u1 = (float)j / ntheta, u2 = (float)(j + 1) / ntheta, v1 = (float)i / nt, v2 = (float)(i + 1) / nt;
        pv(v, a, na, u1, v1, col); pv(v, c, na, u1, v2, col); pv(v, b, nb, u2, v1, col);
        pv(v, b, nb, u2, v1, col); pv(v, c, na, u1, v2, col); pv(v, d, nb, u2, v2, col);
    }
}

// ======== GLOBALS ========
const int SW = 1600, SH = 900;
Camera camera(vec3(0, 3.5f, 55));
Camera freeCamera(vec3(0, 5, 0));
float lastX = SW / 2.f, lastY = SH / 2.f;
bool firstMouse = true;
float deltaTime = 0, lastFrame = 0;
bool fourVP = true; int activeVP = 4;
bool usePhong = true, allLightsOff = false, emissiveOn = true, showBez = true, useFreeCamera = false;
bool birdEyeView = false, wireframeMode = false;
float ambStr = 0.4f;
bool litOn[6] = { 1,1,1,1,1,1 };
bool dirLightOn = true, spotLightOn = true;
int demoTexMode = 0;
float doorAngleAcademic = 0, doorAngleHall = 0, doorAngleDept = 0, doorAngleLibrary = 0, doorAngleSWC = 0;

// Day/night
DayNightCycle dayNight;
bool manualNightMode = false;

// Entities
vector<Bird> birds;
vector<StudentNPC> students;
Projectile mainProjectile;
Projectile cricketBall, footballBall;
bool cricketActive = false, footballActive = false;
vector<InteractiveProp> interactiveProps;

// UI Toggles
bool showMap = false;
bool showDirectory = false;
bool showLegend = false;
string buildingIntroText = "";
float buildingIntroTimer = 0.0f;

// Collision
vector<AABB> colliders;

// Curve editing
bool curveEditMode = false;
bool useBSpline = false;
vector<vec3> curveControlPoints;
vector<Mesh> userCurves;

// Object selection
bool selectionMode = false;
int selectedObjIdx = -1;
struct SelectableObj { string name; vec3 pos; vec3 rot; vec3 scl; int type; };
vector<SelectableObj> selectables;

SelectableObj* getSelectable(const string& name) {
    for (auto& s : selectables) if (s.name == name) return &s;
    return nullptr;
}

mat4 makeSelectableModel(const string& name, vec3 basePos = vec3(0), bool useScale = true) {
    SelectableObj* s = getSelectable(name);
    mat4 m(1.0f);
    if (s) {
        m = translate(m, basePos + s->pos);
        m = rotate(m, s->rot.x, vec3(1, 0, 0));
        m = rotate(m, s->rot.y, vec3(0, 1, 0));
        m = rotate(m, s->rot.z, vec3(0, 0, 1));
        if (useScale) m = scale(m, s->scl);
    }
    else {
        m = translate(m, basePos);
    }
    return m;
}

// Auto tour
bool autoTour = false;
float autoTourT = 0;

// Wind
vec3 windDir(0);
bool interiorFansOn = true, interiorACOn = true, interiorLightsOn = true, interiorStairsOn = true;

vec3 lightPos[6] = {
    vec3(8, 8,-50),vec3(22, 8,-50),vec3(-22, 8,-45),
    vec3(-22, 8,-5),vec3(20, 8, 10),vec3(-10, 8, 30)
};

// Meshes
Mesh mGround, mRoad, mAcademicBuilding, mHallBuilding, mDeptBuilding, mLibraryBuilding, mPond, mField;
Mesh mTrunk, mLeaf, mShahid, mLampP, mLampG;
Mesh mBezier, mSpline, mRuled, mTrack, mSWC, mKUETHood;
Mesh mInteriorAcademic, mInteriorHall, mInteriorDept, mInteriorLibrary;
Mesh mDurbarBangla;
Mesh mCarBody, mCarCabin, mCarWheel, mCarHeadlight;
Mesh mBirdBody, mBirdWing;
Mesh mStudentFig;
Mesh mCricketField, mFootballField, mFootballMesh;
Mesh mSunSphere, mMoonSphere;
Mesh mClockFace, mClockHand, mClockTower;
Mesh mProjectileBall;
Mesh mBuildingSigns, mTrafficElements, mRoadMarkings;
Mesh mShore, mDoorPanel;

// Textures
unsigned int texBrick, texConcrete, texGrass, texRoad, texWater, texWood, texMarble;
unsigned int texRedSun, texYellowSun, texMoon;

unsigned int loadTex(const char* path) {
    unsigned int t; glGenTextures(1, &t);
    int w, h, c; stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &w, &h, &c, 0);
    if (data) {
        GLenum fmt = c == 4 ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else cout << "Failed to load texture: " << path << endl;
    stbi_image_free(data); return t;
}

// ======== SCENE SETUP ========
void setupScene() {
    srand(42);
    vector<float> v;

    // Ground
    v.clear(); genPlane(v, vec3(0, 0, 0), 200, 200, vec3(0.3f, 0.6f, 0.2f), 1); mGround = makeMesh(v);

    // Roads - consistent texture with world-space UVs
    v.clear();
    genRoadPlane(v, vec3(0, 0.02f, 0), 6, 120, vec3(0.25f));          // Main N-S road
    genRoadPlane(v, vec3(0, 0.02f, -30), 80, 6, vec3(0.25f));         // E-W road 1
    genRoadPlane(v, vec3(0, 0.02f, 20), 69, 6, vec3(0.25f));          // E-W road 2
   // genRoadPlane(v, vec3(0, 0.02f, -60), 40, 6, vec3(0.25f));         // E-W road 3
    mRoad = makeMesh(v);

    // Academic Building (hollow walls)
    v.clear();
    genBuildingWallsWithDoor(v, vec3(0, 0, -60), vec3(20, 12, 12), vec3(0.85f, 0.65f, 0.45f), -1.0f, 1.0f, 3.4f);
    genBox(v, vec3(0, 5.0f, -60), vec3(20.2f, 0.15f, 12.2f), vec3(0.75f, 0.6f, 0.4f), 1);
    for (int f = 0; f < 2; f++) for (int wi = 0; wi < 6; wi++) {
        float wx = -7.5f + wi * 2.8f, wy = 1.5f + f * 5.5f;
        genBox(v, vec3(wx, wy, -53.9f), vec3(1.5f, 2.2f, 0.1f), vec3(0.5f, 0.65f, 0.8f), 1);
        genBox(v, vec3(wx, wy, -66.1f), vec3(1.5f, 2.2f, 0.1f), vec3(0.5f, 0.65f, 0.8f), 1);
    }
    genBox(v, vec3(0, 0, -53.85f), vec3(2.0f, 3.5f, 0.15f), vec3(0.4f, 0.3f, 0.2f), 1);
    mAcademicBuilding = makeMesh(v);

    // Hall Building (hollow walls)
    v.clear();
    genBuildingWallsWithDoor(v, vec3(30, 0, -55), vec3(14, 10, 10), vec3(0.8f, 0.6f, 0.4f), 29.0f, 1.0f, 3.4f);
    genBox(v, vec3(30, 4.5f, -55), vec3(14.2f, 0.15f, 10.2f), vec3(0.7f, 0.55f, 0.35f), 1);
    for (int f = 0; f < 2; f++) for (int wi = 0; wi < 4; wi++) {
        float wx = 24.5f + wi * 3.5f, wy = 1.5f + f * 4.5f;
        genBox(v, vec3(wx, wy, -49.9f), vec3(1.5f, 2.0f, 0.1f), vec3(0.5f, 0.65f, 0.8f), 1);
    }
    genBox(v, vec3(30, 0, -49.85f), vec3(2.0f, 3.5f, 0.15f), vec3(0.35f, 0.28f, 0.18f), 1);
    mHallBuilding = makeMesh(v);

    // Department (concrete hollow walls)
    v.clear();
    genBuildingWallsWithDoor(v, vec3(-30, 0, -50), vec3(14, 10, 10), vec3(0.7f, 0.7f, 0.7f), -31.0f, 1.0f, 3.4f);
    genBox(v, vec3(-30, 5.0f, -50), vec3(14.1f, 0.15f, 10.1f), vec3(0.65f, 0.65f, 0.65f), 1);
    mDeptBuilding = makeMesh(v);

    // Library (concrete hollow walls)
    v.clear();
    genBuildingWallsWithDoor(v, vec3(-30, 0, -10), vec3(12, 8, 10), vec3(0.75f, 0.75f, 0.7f), -31.0f, 1.0f, 3.4f);
    genBox(v, vec3(-30, 4.0f, -10), vec3(12.1f, 0.15f, 10.1f), vec3(0.7f, 0.7f, 0.65f), 1);
    mLibraryBuilding = makeMesh(v);

    // SWC (hollow walls)
    v.clear();
    genBuildingWallsWithDoor(v, vec3(30, 0, 15), vec3(12, 8, 14), vec3(0.7f, 0.35f, 0.25f), 29.0f, 1.0f, 3.4f);
    for (int i = 0; i < 5; i++) genBox(v, vec3(23.5f, 0, 10.f + i * 2.5f), vec3(0.4f, 8, 0.4f), vec3(0.9f), 1);
    genBox(v, vec3(22, 7.5f, 15), vec3(4, 0.3f, 14), vec3(0.85f), 1);
    mSWC = makeMesh(v);

    // Ruled surface (SWC curved wall)
    v.clear();
    vector<vec3> rc1, rc2;
    for (int i = 0; i <= 20; i++) {
        float a = (float)i / 20 * (float)M_PI;
        float rx = 36 + 5 * cos(a), rz = 15 + 7 * sin(a);
        rc1.push_back(vec3(rx, 0, rz)); rc2.push_back(vec3(rx, 8, rz));
    }
    genRuled(v, rc1, rc2, vec3(0.7f, 0.35f, 0.25f));
    mRuled = makeMesh(v);

    // Enhanced Pond/Lake
    v.clear(); vector<float> sv2;
    genEnhancedLake(v, sv2);
    mPond = makeMesh(v);
    mShore = makeMesh(sv2);

    // Central green zone
    v.clear(); genPlane(v, vec3(-5, 0.01f, 32), 40, 28, vec3(0.25f, 0.55f, 0.2f), 1); mField = makeMesh(v);

    // Track (spline)
    v.clear();
    vector<vec3> tp;
    for (int i = 0; i <= 40; i++) {
        float a = (float)i / 40 * 2 * (float)M_PI;
        tp.push_back(vec3(-30 + 18 * cos(a), 0.03f, 30 + 13 * sin(a)));
    }
    tp.push_back(tp[1]); tp.push_back(tp[2]); tp.insert(tp.begin(), tp[tp.size() - 4]);
    genSplineRibbon(v, tp, 0.3f, vec3(0.9f), 10);
    mTrack = makeMesh(v);

    // Shahid Minar
    v.clear(); genShahidMinar(v); mShahid = makeMesh(v);

    // Durbar Bangla Sculpture
    v.clear(); genDurbarBangla(v); mDurbarBangla = makeMesh(v);

    // Trees (improved fractal)
    vector<float> trk, lf;
    vec3 treePos[] = {
        vec3(-15,0,-40),vec3(15,0,-40),vec3(-50,0,0),vec3(50,0,0),
        vec3(-20,0,40),vec3(20,0,40),vec3(-55,0,30),vec3(45,0,-25),
        vec3(-10,0,10),vec3(40,0,35),vec3(-50,0,-50),vec3(50,0,-40)
    };
    for (auto& tp2 : treePos) genTree(trk, lf, tp2, vec3(0, 1, 0), 3.0f, 0.2f, 0, 3);
    mTrunk = makeMesh(trk); mLeaf = makeMesh(lf);

    // Lamp posts
    v.clear();
    for (int i = 0; i < 6; i++) genCyl(v, vec3(lightPos[i].x, 0, lightPos[i].z), lightPos[i], 0.1f, vec3(0.3f));
    mLampP = makeMesh(v);
    v.clear();
    for (int i = 0; i < 6; i++) genSphere(v, lightPos[i] + vec3(0, 0.5f, 0), 0.4f, vec3(1.0f, 0.95f, 0.7f), 8, 8);
    mLampG = makeMesh(v);

    // Bezier arch
    v.clear();
    genBezierTube(v, vec3(-4, 0, 55), vec3(-4, 10, 55), vec3(4, 10, 55), vec3(4, 0, 55), 0.15f, vec3(0.9f), 20, 6);
    genBezierTube(v, vec3(-4, 0, 55), vec3(-4, 8, 57), vec3(4, 8, 57), vec3(4, 0, 55), 0.1f, vec3(0.85f), 20, 6);
    mBezier = makeMesh(v);

    // Walkway spline (vertex color only, no concrete texture)
    v.clear();
    vector<vec3> wp = { vec3(0,0.02f,50),vec3(5,0.02f,40),vec3(-3,0.02f,30),vec3(2,0.02f,20),
                     vec3(-2,0.02f,10),vec3(3,0.02f,0),vec3(0,0.02f,-10),vec3(-5,0.02f,-20),
                     vec3(0,0.02f,-30),vec3(5,0.02f,-40),vec3(0,0.02f,-50),vec3(-3,0.02f,-55) };
    genSplineRibbon(v, wp, 0.5f, vec3(0.55f, 0.52f, 0.48f), 12);
    mSpline = makeMesh(v);

    // KUET Hood
    v.clear(); genKUETHood(v, vec3(-45, 0, 45)); mKUETHood = makeMesh(v);

    // Building interiors (detailed 2-story)
    v.clear(); genAcademicBuildingInteriorFull(v, vec3(0, 0, -60)); mInteriorAcademic = makeMesh(v);
    v.clear(); genHallBuildingInteriorFull(v, vec3(30, 0, -55)); mInteriorHall = makeMesh(v);
    v.clear(); genBuildingInterior(v, vec3(-30, 0, -50), vec3(14, 10, 10), vec3(0.8f, 0.8f, 0.9f)); mInteriorDept = makeMesh(v);
    v.clear(); genBuildingInterior(v, vec3(-30, 0, -10), vec3(12, 8, 10), vec3(0.85f, 0.8f, 0.7f)); mInteriorLibrary = makeMesh(v);

    // Building name signs
    v.clear();
    genBuildingSign(v, vec3(0, 11.5f, -53.8f), vec3(0.2f, 0.4f, 0.8f));       // Academic - Blue
    genBuildingSign(v, vec3(30, 9.5f, -49.8f), vec3(0.2f, 0.7f, 0.3f));       // Hall - Green
    genBuildingSign(v, vec3(-30, 9.5f, -44.8f), vec3(0.5f, 0.5f, 0.5f));      // Dept - Gray
    genBuildingSign(v, vec3(-30, 7.5f, -4.8f), vec3(0.8f, 0.7f, 0.2f));       // Library - Yellow
    genBuildingSign(v, vec3(30, 7.5f, 22.2f), vec3(0.8f, 0.2f, 0.2f));        // SWC - Red
    mBuildingSigns = makeMesh(v);

    // Clock tower on Academic building
    v.clear(); genClockTower(v); mClockTower = makeMesh(v);

    // Traffic elements (replacing demo sphere/cone)
    v.clear();
    genTrafficCone(v, vec3(3, 0.02f, -30));    // Road intersection 1
    genTrafficCone(v, vec3(-3, 0.02f, -30));   // Road intersection 1
    genTrafficCone(v, vec3(3, 0.02f, 20));     // Road intersection 2
    genTrafficCone(v, vec3(-3, 0.02f, 20));    // Road intersection 2
    genBollard(v, vec3(3.5f, 0.02f, -10));
    genBollard(v, vec3(-3.5f, 0.02f, -10));
    genBollard(v, vec3(3.5f, 0.02f, 10));
    genBollard(v, vec3(-3.5f, 0.02f, 10));
    mTrafficElements = makeMesh(v);

    // Road markings
    v.clear(); genRoadMarkings(v); mRoadMarkings = makeMesh(v);

    // Separate sports fields
    v.clear(); genFullCricketField(v); mCricketField = makeMesh(v);
    v.clear(); genFullFootballField(v); mFootballField = makeMesh(v);
    v.clear(); genFootball(v, vec3(0)); mFootballMesh = makeMesh(v);

    // Door panel (hinge at edge instead of center)
    v.clear(); genBox(v, vec3(0.5f, 0, 0), vec3(1.0f, 3.4f, 0.1f), vec3(0.4f, 0.3f, 0.2f), 1); mDoorPanel = makeMesh(v);

    // ======== HIERARCHICAL CAR PARTS ========
    v.clear(); genBox(v, vec3(0, 0.3f, 0), vec3(1.8f, 0.6f, 3.5f), vec3(0.2f, 0.35f, 0.7f), 1); mCarBody = makeMesh(v);
    v.clear();
    genBox(v, vec3(0, 0.9f, -0.3f), vec3(1.5f, 0.7f, 1.8f), vec3(0.18f, 0.3f, 0.6f), 1);
    genBox(v, vec3(0, 0.95f, 0.62f), vec3(1.3f, 0.5f, 0.05f), vec3(0.5f, 0.6f, 0.7f), 1);
    mCarCabin = makeMesh(v);
    v.clear(); genCyl(v, vec3(-0.15f, 0, 0), vec3(0.15f, 0, 0), 0.3f, vec3(0.15f), 10);
    genDisc(v, vec3(0.16f, 0, 0), 0.2f, vec3(0.4f), vec3(1, 0, 0), 8); mCarWheel = makeMesh(v);
    v.clear(); genSphere(v, vec3(0, 0, 0), 0.12f, vec3(1.0f, 0.95f, 0.7f), 6, 6); mCarHeadlight = makeMesh(v);

    // Bird body & wing
    v.clear(); genBirdBody(v); mBirdBody = makeMesh(v);
    v.clear(); genBirdWing(v); mBirdWing = makeMesh(v);

    // Student figure
    v.clear(); genStudentFigure(v); mStudentFig = makeMesh(v);

    // Sun/Moon spheres (textured)
    v.clear(); genSphere(v, vec3(0), 1.0f, vec3(1), 36, 36); mSunSphere = makeMesh(v);
    v.clear(); genSphere(v, vec3(0), 1.0f, vec3(1), 36, 36); mMoonSphere = makeMesh(v);

    // Clock UI
    v.clear(); genDisc(v, vec3(0), 1.0f, vec3(0.9f, 0.9f, 0.85f), vec3(0, 0, 1), 24); mClockFace = makeMesh(v);
    v.clear(); genRect2D(v, 1.0f, 0.04f, vec3(0.1f)); mClockHand = makeMesh(v);

    // Projectile ball
    v.clear(); genSphere(v, vec3(0), 0.15f, vec3(0.9f, 0.1f, 0.1f), 8, 8); mProjectileBall = makeMesh(v);

    // ======== LOAD TEXTURES ========
    texGrass = loadTex("grass.jpg");
    texBrick = loadTex("brick.jpg");
    texConcrete = loadTex("concrete.jpg");
    texRoad = loadTex("road.png");
    texWater = loadTex("water.jpg");
    texWood = loadTex("wood.jpg");
    texMarble = loadTex("marble.jpeg");
    texRedSun = loadTex("redsun.png");
    texYellowSun = loadTex("yellowsun.png");
    texMoon = loadTex("moon.png");

    // ======== SETUP COLLISION BOXES ========
    colliders.push_back(makeAABB(0, 0, -60, 10, 12, 6));       // Academic
    colliders.push_back(makeAABB(30, 0, -55, 7, 10, 5));       // Hall
    colliders.push_back(makeAABB(-30, 0, -50, 7, 10, 5));      // Dept
    colliders.push_back(makeAABB(-30, 0, -10, 6, 8, 5));       // Library
    colliders.push_back(makeAABB(30, 0, 15, 6, 8, 7));         // SWC
    // Shahid Minar - tighter multi-AABB collision
    //colliders.push_back(makeAABB(-40, 0, 10, 8, 1, 5));        // Base platform
    for (int i = 0; i < 6; i++)
        colliders.push_back(makeAABB(-44.0f + i * 2.0f, 0, 10, 0.5f, 12, 0.5f)); // Individual pillars
    colliders.push_back(makeAABB(45, 0, -50, 4, 6, 4));        // Durbar Bangla
    colliders.push_back(makeAABB(-45, 0, 45, 2.5f, 3, 2.5f));  // KUET Hood
    colliders.push_back(makeAABB(26, 0, -8, 12.5f, 2.0f, 9.5f)); // Lake -> prevent car plunging
    colliders.push_back(makeAABB(-40, 0, 10, 8, 1, 5));
    // Tree trunks
    vec3 treePosList[] = {
        vec3(-15,0,-40),vec3(15,0,-40),vec3(-50,0,0),vec3(50,0,0),
        vec3(-20,0,40),vec3(20,0,40),vec3(-55,0,30),vec3(45,0,-25),
        vec3(-10,0,10),vec3(40,0,35),vec3(-50,0,-30),vec3(50,0,-40)
    };
    for (auto& tp3 : treePosList)
        colliders.push_back(makeAABB(tp3.x, 0, tp3.z, 0.5f, 5, 0.5f));
    // Lamp posts
    for (int i = 0; i < 6; i++)
        colliders.push_back(makeAABB(lightPos[i].x, 0, lightPos[i].z, 0.3f, 8, 0.3f));

    // ======== SETUP ENTITIES ========
    birds.resize(5);
    for (int i = 0; i < 5; i++) {
        birds[i].position = vec3((rand() % 80) - 40, 12.0f + (rand() % 10), (rand() % 80) - 40);
        birds[i].size = 0.3f + (rand() % 10) * 0.02f;
    }

    // Students (12 for classrooms/halls)
    students.resize(12);
    for (int i = 0; i < 12; i++) {
        // Distribute to academic
        float bx = (i % 2 == 0) ? -5 : 5;
        float bz = (i % 4 < 2) ? -63 : -57;
        students[i].setWaypoints({ vec3(bx + (i%2), 0, 0), vec3(bx, 0, -30), vec3(bx, 0, bz) });
        students[i].speed = 1.5f + (rand() % 100) * 0.01f;
    }

    // Selectable objects
    selectables.push_back({ "Sun", vec3(0), vec3(0), vec3(1), 0 });
    selectables.push_back({ "Moon", vec3(0), vec3(0), vec3(1), 1 });
    selectables.push_back({ "AcademicBuilding", vec3(0), vec3(0), vec3(1), 2 });
    selectables.push_back({ "HallBuilding", vec3(0), vec3(0), vec3(1), 3 });
    selectables.push_back({ "DeptBuilding", vec3(0), vec3(0), vec3(1), 4 });
    selectables.push_back({ "LibraryBuilding", vec3(0), vec3(0), vec3(1), 5 });
    selectables.push_back({ "SWC", vec3(0), vec3(0), vec3(1), 6 });
    selectables.push_back({ "Lake", vec3(0), vec3(0), vec3(1), 7 });
    selectables.push_back({ "ShahidMinar", vec3(0), vec3(0), vec3(1), 8 });
    selectables.push_back({ "DurbarBangla", vec3(0), vec3(0), vec3(1), 9 });
    selectables.push_back({ "BezierArch", vec3(0), vec3(0), vec3(1), 10 });
    selectables.push_back({ "WalkwaySpline", vec3(0), vec3(0), vec3(1), 11 });
    selectables.push_back({ "KUETHood", vec3(0), vec3(0), vec3(1), 12 });
    selectables.push_back({ "CricketField", vec3(0), vec3(0), vec3(1), 13 });
    selectables.push_back({ "FootballField", vec3(0), vec3(0), vec3(1), 14 });
    selectables.push_back({ "Football", vec3(-52, 0.2f, -28), vec3(0), vec3(1), 15 });

    // Basic layout sanity checks to catch obvious overlaps.
    auto tooClose = [](vec3 a, vec3 b, float minDist) { return length(vec2(a.x - b.x, a.z - b.z)) < minDist; };
    if (tooClose(vec3(26, 0, -8), vec3(0, 0, 0), 10.0f)) {
        cout << "[Layout warning] Lake too close to main road center." << endl;
    }

    // Setup interactive props
    interactiveProps.clear();
    interactiveProps.push_back(InteractiveProp(PROP_DOOR, vec3(-1.5f, 0, -53.85f))); // Academic
    interactiveProps.push_back(InteractiveProp(PROP_DOOR, vec3(28.5f, 0, -49.85f))); // Hall
    interactiveProps.push_back(InteractiveProp(PROP_DOOR, vec3(-31.5f, 0, -44.85f))); // Dept
    interactiveProps.push_back(InteractiveProp(PROP_DOOR, vec3(-31.5f, 0, -4.85f)));  // Lib
    interactiveProps.push_back(InteractiveProp(PROP_DOOR, vec3(28.5f, 0, 22.15f)));   // SWC
    
    // Some fans and taps
    interactiveProps.push_back(InteractiveProp(PROP_FAN, vec3(-5.0f, 4.6f, -63.0f))); // Academic classroom
    interactiveProps.push_back(InteractiveProp(PROP_FAN, vec3(5.0f, 4.6f, -63.0f)));
    interactiveProps.push_back(InteractiveProp(PROP_TAP, vec3(35.5f, 1.4f, -57.0f))); // Washroom tap

    // Setup car camera
    camera.groundClamped = true;
    camera.groundY = 2.5f;
}

// ======== SET UNIFORMS ========
void setMat(Shader& s, vec3 a, vec3 d, vec3 sp, float sh, vec3 em) {
    s.setVec3("mat1.ambient", a); s.setVec3("mat1.diffuse", d);
    s.setVec3("mat1.specular", sp); s.setFloat("mat1.shininess", sh);
    s.setVec3("mat1.emissive", em);
}

void setLights(Shader& s) {
    s.setInt("numPointLights", 6);
    s.setBool("allOff", allLightsOff);
    s.setFloat("ambStr", ambStr);

    // Directional light (sun) - controlled by day/night cycle
    bool useDirLight = dirLightOn && dayNight.dirLightActive && !manualNightMode;
    s.setBool("dirLightOn", useDirLight);
    s.setBool("spotLightOn", spotLightOn);

    float sunScaleForLight = 1.0f;
    if (SelectableObj* sunSel = getSelectable("Sun")) {
        sunScaleForLight = clamp(sunSel->scl.x, 0.5f, 2.5f);
    }
    s.setVec3("dirLight.direction", dayNight.sunDir);
    s.setVec3("dirLight.ambient", dayNight.sunColor * (0.18f * sunScaleForLight));
    s.setVec3("dirLight.diffuse", dayNight.sunColor * sunScaleForLight);
    s.setVec3("dirLight.specular", dayNight.sunColor * (0.45f * sunScaleForLight));

    // Spotlight (car headlights)
    vec3 vanPos = camera.Position - vec3(0, 2.0f, 0);
    s.setVec3("spotLight.position", vanPos + vec3(0, 0.8f, 0));
    s.setVec3("spotLight.direction", camera.Front);
    s.setVec3("spotLight.ambient", vec3(0.05f));
    s.setVec3("spotLight.diffuse", vec3(0.9f, 0.7f, 0.3f));
    s.setVec3("spotLight.specular", vec3(0.8f));
    s.setFloat("spotLight.constant", 1.0f);
    s.setFloat("spotLight.linear", 0.06f);
    s.setFloat("spotLight.quadratic", 0.018f);
    s.setFloat("spotLight.cutOff", cos(radians(12.5f)));
    s.setFloat("spotLight.outerCutOff", cos(radians(15.0f)));

    // Point lights (with gentle sway for motion)
    float time = (float)glfwGetTime();
    for (int i = 0; i < 6; i++) {
        string b = "pointLights[" + to_string(i) + "].";
        vec3 lp = lightPos[i] + vec3(sin(time * 0.5f + i) * 0.1f, 0, cos(time * 0.3f + i) * 0.1f);
        s.setVec3(b + "position", lp);

        // During night, point lights are brighter
        float nightBoost = (dayNight.phase == PHASE_NIGHT || manualNightMode) ? 1.5f : 1.0f;
        s.setVec3(b + "ambient", vec3(0.1f) * nightBoost);
        s.setVec3(b + "diffuse", vec3(0.7f) * nightBoost);
        s.setVec3(b + "specular", vec3(1.0f));
        s.setFloat(b + "constant", 1.0f);
        s.setFloat(b + "linear", 0.035f);
        s.setFloat(b + "quadratic", 0.0045f);
        s.setBool("lightOn[" + to_string(i) + "]", litOn[i]);
    }
}

// ======== RENDER HIERARCHICAL CAR ========
void renderCar(Shader& s, vec3 carPos, float yaw, float wheelRot) {
    s.setInt("texMode", 2);
    s.setBool("windEnabled", false);

    // Root transform (car position + orientation)
    mat4 root = translate(mat4(1.0f), carPos);
    root = rotate(root, radians(yaw - 90.f), vec3(0, 1, 0));

    // Body
    s.setMat4("model", root);
    setMat(s, vec3(0.2f), vec3(0.3f, 0.4f, 0.7f), vec3(0.4f), 32, vec3(0));
    drawMesh(mCarBody);

    // Cabin
    s.setMat4("model", root);
    setMat(s, vec3(0.15f), vec3(0.25f, 0.35f, 0.65f), vec3(0.3f), 32, vec3(0));
    drawMesh(mCarCabin);

    // Wheels - hierarchical: parent = root, child = wheel with local transform + spin
    vec3 wheelPositions[] = { vec3(-0.95f, 0.3f, 1.0f), vec3(0.95f, 0.3f, 1.0f), vec3(-0.95f, 0.3f, -1.0f), vec3(0.95f, 0.3f, -1.0f) };
    for (int i = 0; i < 4; i++) {
        mat4 wheelMat = root;
        wheelMat = translate(wheelMat, wheelPositions[i]);
        wheelMat = rotate(wheelMat, wheelRot, vec3(1, 0, 0)); // Spin
        s.setMat4("model", wheelMat);
        setMat(s, vec3(0.1f), vec3(0.15f), vec3(0.05f), 8, vec3(0));
        drawMesh(mCarWheel);
    }

    // Headlights (emissive at night)
    vec3 hlPositions[] = { vec3(-0.6f, 0.6f, 1.75f), vec3(0.6f, 0.6f, 1.75f) };
    bool nightTime = (dayNight.phase == PHASE_NIGHT || manualNightMode);
    for (int i = 0; i < 2; i++) {
        mat4 hlMat = translate(root, hlPositions[i]);
        s.setMat4("model", hlMat);
        setMat(s, vec3(0.5f), vec3(1.0f, 0.95f, 0.7f), vec3(0.5f), 32, nightTime ? vec3(1.0f, 0.9f, 0.5f) : vec3(0.3f, 0.25f, 0.1f));
        drawMesh(mCarHeadlight);
    }
}

// ======== RENDER SCENE ========
void renderScene(Shader& s, mat4 view, mat4 proj, bool drawCarModel, bool isInteriorView = false) {
    s.use();
    s.setMat4("view", view); s.setMat4("projection", proj);
    vec3 activePos = useFreeCamera ? freeCamera.Position : camera.Position;
    s.setVec3("viewPos", activePos);
    s.setFloat("timeVal", (float)glfwGetTime());
    setLights(s);
    s.setBool("windEnabled", false);
    s.setVec3("windOffset", windDir);
    mat4 model(1.0f);
    s.setMat4("model", model);

    // Ground
    s.setInt("texMode", 1); glBindTexture(GL_TEXTURE_2D, texGrass);
    setMat(s, vec3(0.8f), vec3(1.0f), vec3(0.05f), 16, vec3(0));
    drawMesh(mGround);

    // Roads
    s.setInt("texMode", 1);
    glBindTexture(GL_TEXTURE_2D, texRoad);
    setMat(s, vec3(0.8f), vec3(1.0f), vec3(0.05f), 8, vec3(0));
    drawMesh(mRoad);

    // Buildings (brick): academic + hall
    s.setInt("texMode", 1); glBindTexture(GL_TEXTURE_2D, texBrick);
    setMat(s, vec3(0.3f), vec3(0.85f, 0.65f, 0.45f), vec3(0.2f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("AcademicBuilding"));
    drawMesh(mAcademicBuilding);
    s.setMat4("model", makeSelectableModel("HallBuilding"));
    drawMesh(mHallBuilding);

    // Buildings (concrete): department + library
    s.setInt("texMode", 0); glBindTexture(GL_TEXTURE_2D, texConcrete);
    setMat(s, vec3(0.3f), vec3(0.7f), vec3(0.2f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("DeptBuilding"));
    drawMesh(mDeptBuilding);
    s.setMat4("model", makeSelectableModel("LibraryBuilding"));
    drawMesh(mLibraryBuilding);

    // SWC
    s.setInt("texMode", 1); glBindTexture(GL_TEXTURE_2D, texBrick);
    setMat(s, vec3(0.25f), vec3(0.7f, 0.35f, 0.25f), vec3(0.3f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("SWC"));
    drawMesh(mSWC);

    // Ruled surface
    s.setInt("texMode", 3);
    setMat(s, vec3(0.25f), vec3(0.7f, 0.35f, 0.25f), vec3(0.3f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("SWC"));
    drawMesh(mRuled);

    // Enhanced Pond (animated water)
    s.setInt("texMode", 5); glBindTexture(GL_TEXTURE_2D, texWater);
    setMat(s, vec3(0.15f), vec3(0.4f, 0.6f, 0.8f), vec3(0.7f), 128, vec3(0.05f, 0.08f, 0.12f));
    s.setMat4("model", makeSelectableModel("Lake"));
    drawMesh(mPond);
    // Shoreline
    s.setInt("texMode", 3);
    setMat(s, vec3(0.3f), vec3(0.65f, 0.55f, 0.35f), vec3(0.05f), 8, vec3(0));
    s.setMat4("model", makeSelectableModel("Lake"));
    drawMesh(mShore);

    // Field
    glBindTexture(GL_TEXTURE_2D, texGrass);
    setMat(s, vec3(0.2f), vec3(0.5f), vec3(0.05f), 8, vec3(0));
    s.setMat4("model", makeSelectableModel("CricketField"));
    drawMesh(mField);

    // Track
    s.setInt("texMode", 3);
    setMat(s, vec3(0.8f), vec3(0.95f), vec3(0.3f), 16, vec3(0));
    s.setMat4("model", makeSelectableModel("CricketField"));
    drawMesh(mTrack);

    // Shahid Minar
    setMat(s, vec3(0.4f), vec3(0.95f), vec3(0.5f), 64, vec3(0));
    s.setMat4("model", makeSelectableModel("ShahidMinar"));
    drawMesh(mShahid);

    // Durbar Bangla Sculpture
    s.setInt("texMode", 1); glBindTexture(GL_TEXTURE_2D, texConcrete);
    setMat(s, vec3(0.3f), vec3(0.65f, 0.6f, 0.55f), vec3(0.3f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("DurbarBangla"));
    drawMesh(mDurbarBangla);

    // Trees - trunk
    s.setInt("texMode", 3);
    s.setBool("windEnabled", false);
    setMat(s, vec3(0.15f), vec3(0.45f, 0.25f, 0.12f), vec3(0.05f), 8, vec3(0));
    drawMesh(mTrunk);

    // Trees - leaves (with wind)
    s.setInt("texMode", 2);
    s.setBool("windEnabled", true);
    s.setVec3("windOffset", windDir);
    setMat(s, vec3(0.2f), vec3(0.4f, 0.7f, 0.15f), vec3(0.05f), 8, vec3(0));
    drawMesh(mLeaf);
    s.setBool("windEnabled", false);

    // Lamp posts
    s.setInt("texMode", 3);
    setMat(s, vec3(0.1f), vec3(0.3f), vec3(0.1f), 16, vec3(0));
    drawMesh(mLampP);

    // Lamp glows
    setMat(s, vec3(0.5f), vec3(1, 0.95f, 0.7f), vec3(0.5f), 32, emissiveOn ? vec3(0.8f, 0.7f, 0.4f) : vec3(0));
    drawMesh(mLampG);

    // Bezier arch
    if (showBez) {
        setMat(s, vec3(0.3f), vec3(0.9f), vec3(0.5f), 64, vec3(0));
        s.setMat4("model", makeSelectableModel("BezierArch"));
        drawMesh(mBezier);
    }

    // Walkway spline (vertex color, no concrete texture)
   /* s.setInt("texMode", 3);
    setMat(s, vec3(0.3f), vec3(0.55f, 0.52f, 0.48f), vec3(0.1f), 16, vec3(0));
    s.setMat4("model", makeSelectableModel("WalkwaySpline"));
    drawMesh(mSpline);*/

    // KUET Hood
   /* s.setInt("texMode", 1); glBindTexture(GL_TEXTURE_2D, texMarble);
    setMat(s, vec3(0.3f), vec3(0.9f, 0.85f, 0.7f), vec3(0.4f), 48, vec3(0));
    s.setMat4("model", makeSelectableModel("KUETHood"));
    drawMesh(mKUETHood);*/

    // Traffic elements (cones and bollards)
    s.setInt("texMode", 2);
    setMat(s, vec3(0.3f), vec3(0.9f, 0.4f, 0.1f), vec3(0.2f), 16, vec3(0));
    drawMesh(mTrafficElements);

    // Road markings
    s.setInt("texMode", 3);
    setMat(s, vec3(0.8f), vec3(0.95f), vec3(0.1f), 8, vec3(0));
    drawMesh(mRoadMarkings);

    // Building name signs (emissive at night for visibility)
    s.setInt("texMode", 2);
    { bool isNight = (dayNight.phase == PHASE_NIGHT || manualNightMode);
      setMat(s, vec3(0.4f), vec3(0.8f), vec3(0.3f), 32, isNight ? vec3(0.4f, 0.3f, 0.2f) : vec3(0));
      s.setMat4("model", makeSelectableModel("AcademicBuilding"));
      drawMesh(mBuildingSigns); }

    // Clock tower
    s.setInt("texMode", 2);
    setMat(s, vec3(0.3f), vec3(0.8f, 0.7f, 0.55f), vec3(0.3f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("AcademicBuilding"));
    drawMesh(mClockTower);

    // 3D clock hands on building
    { time_t rawtime; time(&rawtime); struct tm* ti = localtime(&rawtime);
      float secA = -(ti->tm_sec * 6.0f - 90.0f) * (float)M_PI / 180.0f;
      float minA = -(ti->tm_min * 6.0f + ti->tm_sec * 0.1f - 90.0f) * (float)M_PI / 180.0f;
      float hrA = -((ti->tm_hour % 12) * 30.0f + ti->tm_min * 0.5f - 90.0f) * (float)M_PI / 180.0f;
      vec3 cc = vec3(makeSelectableModel("AcademicBuilding") * vec4(0, 10.5f, -52.1f, 1.0f));
      s.setInt("texMode", 3);
      auto drawHand = [&](float angle, float len, float thick, vec3 col) {
          mat4 m = translate(mat4(1.0f), cc); m = rotate(m, angle, vec3(0,0,1));
          m = scale(m, vec3(len, thick, 0.03f)); s.setMat4("model", m);
          setMat(s, col, col, vec3(0.05f), 8, vec3(0)); drawMesh(mClockHand);
      };
      drawHand(hrA, 0.5f, 0.04f, vec3(0.15f));
      drawHand(minA, 0.7f, 0.03f, vec3(0.2f));
      drawHand(secA, 0.85f, 0.01f, vec3(0.8f, 0.1f, 0.1f));
      s.setMat4("model", mat4(1.0f)); }

    // Separate sports fields
    s.setInt("texMode", 3);
    setMat(s, vec3(0.3f), vec3(0.7f, 0.55f, 0.35f), vec3(0.1f), 16, vec3(0));
    s.setMat4("model", makeSelectableModel("CricketField"));
    drawMesh(mCricketField);
    setMat(s, vec3(0.4f), vec3(0.9f), vec3(0.3f), 32, vec3(0));
    s.setMat4("model", makeSelectableModel("FootballField"));
    drawMesh(mFootballField);

    // Football on field
    s.setInt("texMode", 2);
    if (!footballBall.active) {
        SelectableObj* footballSel = getSelectable("Football");
        vec3 footballPos = footballSel ? footballSel->pos : vec3(-52, 0.2f, -28);
        mat4 fbm = translate(mat4(1.0f), footballPos);
        if (footballSel) {
            fbm = rotate(fbm, footballSel->rot.y, vec3(0, 1, 0));
            fbm = scale(fbm, footballSel->scl);
        }
        s.setMat4("model", fbm);
        drawMesh(mFootballMesh);
        s.setMat4("model", mat4(1.0f));
    }

    // Door panels (animated from interactiveProps)
    s.setInt("texMode", 3);
    for(size_t i=0; i<interactiveProps.size(); i++) {
        if(interactiveProps[i].type == PROP_DOOR) {
            mat4 dm = translate(mat4(1.0f), interactiveProps[i].position);
            // Door swings open 90 degrees when active
            float doorAngle = interactiveProps[i].value * 90.0f; // 0 to 90 degrees
            dm = rotate(dm, radians(doorAngle), vec3(0, 1.0f, 0));
            s.setMat4("model", dm); setMat(s, vec3(0.2f), vec3(0.4f, 0.3f, 0.2f), vec3(0.1f), 16, vec3(0));
            drawMesh(mDoorPanel); s.setMat4("model", mat4(1.0f));
        } else if(interactiveProps[i].type == PROP_FAN) {
            // Drawn specifically in proximity below
        } else if(interactiveProps[i].type == PROP_TAP) {
            if(interactiveProps[i].active) {
                // Draw simple spray particles
                for(int p=0; p<10; p++) {
                    float sprayT = interactiveProps[i].value * 3.0f + p * 0.1f;
                    float py = 1.4f - fmod(sprayT, 1.0f);
                    float px = 35.5f + (rand()%10-5)*0.01f;
                    float pz = -57.0f + (rand()%10-5)*0.01f;
                    mat4 sm = translate(mat4(1.0f), vec3(px, py, pz));
                    sm = scale(sm, vec3(0.02f));
                    s.setMat4("model", sm);
                    setMat(s, vec3(0.4f, 0.6f, 0.9f), vec3(0.4f, 0.6f, 0.9f), vec3(0.1f), 8, vec3(0.2f, 0.4f, 0.8f));
                    drawMesh(mProjectileBall);
                }
                s.setMat4("model", mat4(1.0f));
            }
        }
    }

    // Building interiors (when camera is near)
    vec3 camPos = useFreeCamera ? freeCamera.Position : camera.Position;
    if (!isInteriorView) {
        vec3 interiorGlow = interiorLightsOn ? vec3(0.65f, 0.52f, 0.28f) : vec3(0.08f, 0.05f, 0.03f);
        if (abs(camPos.x) < 15 && camPos.z < -50 && camPos.z > -70) {
            s.setInt("texMode", 2);
            setMat(s, vec3(0.5f), vec3(0.9f, 0.8f, 0.6f), vec3(0.3f), 32, interiorGlow);
            drawMesh(mInteriorAcademic);
        }
        if (abs(camPos.x - 30) < 10 && camPos.z < -45 && camPos.z > -65) {
            s.setInt("texMode", 2);
            setMat(s, vec3(0.5f), vec3(0.85f, 0.8f, 0.7f), vec3(0.3f), 32, interiorGlow);
            drawMesh(mInteriorHall);
        }
        if (abs(camPos.x + 30) < 10 && camPos.z < -40 && camPos.z > -60) {
            s.setInt("texMode", 2);
            setMat(s, vec3(0.5f), vec3(0.8f, 0.8f, 0.9f), vec3(0.3f), 32, interiorGlow);
            drawMesh(mInteriorDept);
        }
        if (abs(camPos.x + 30) < 8 && camPos.z < 0 && camPos.z > -20) {
            s.setInt("texMode", 2);
            setMat(s, vec3(0.5f), vec3(0.85f, 0.8f, 0.7f), vec3(0.3f), 32, interiorGlow);
            drawMesh(mInteriorLibrary);
        }

        // Extra interactive interior props (fans, AC, lights, stair indicators)
        auto drawInteractiveFan = [&](vec3 center, float height, float baseSpin) {
            float spin = baseSpin + interiorFansOn ? ((float)glfwGetTime() * 7.0f + baseSpin) : baseSpin;
            // Also check for dynamic prop override
            for(auto& prop : interactiveProps) {
                if(prop.type == PROP_FAN && length(prop.position - center) < 2.0f) {
                    spin = prop.value;
                }
            }
            mat4 hub = translate(mat4(1.0f), center + vec3(0, height, 0));
            hub = scale(hub, vec3(0.12f));
            s.setInt("texMode", 3);
            s.setMat4("model", hub);
            setMat(s, vec3(0.2f), vec3(0.5f, 0.5f, 0.55f), vec3(0.2f), 8, vec3(0));
            drawMesh(mProjectileBall);
            for (int i = 0; i < 4; i++) {
                mat4 blade = translate(mat4(1.0f), center + vec3(0, height, 0));
                blade = rotate(blade, spin + i * (float)M_PI * 0.5f, vec3(0, 1, 0));
                blade = scale(blade, vec3(0.6f, 0.01f, 0.08f));
                s.setMat4("model", blade);
                setMat(s, vec3(0.25f), vec3(0.65f, 0.6f, 0.55f), vec3(0.08f), 8, vec3(0));
                drawMesh(mClockHand);
            }
        };
        auto drawAC = [&](vec3 p) {
            mat4 acm = translate(mat4(1.0f), p);
            acm = scale(acm, vec3(0.45f, 0.2f, 0.12f));
            s.setMat4("model", acm);
            setMat(s, vec3(0.4f), vec3(0.9f, 0.92f, 0.95f), vec3(0.2f), 16, interiorACOn ? vec3(0.08f, 0.14f, 0.2f) : vec3(0));
            drawMesh(mDoorPanel);
        };
        auto drawStairPulse = [&](vec3 p) {
            if (!interiorStairsOn) return;
            mat4 sm = translate(mat4(1.0f), p + vec3(0, 0.2f + 0.2f * sin((float)glfwGetTime() * 4.0f), 0));
            sm = scale(sm, vec3(0.08f));
            s.setMat4("model", sm);
            setMat(s, vec3(0.3f), vec3(0.9f, 0.75f, 0.25f), vec3(0.3f), 8, vec3(0.2f, 0.12f, 0.03f));
            drawMesh(mProjectileBall);
        };
        if (abs(camPos.x) < 18 && camPos.z < -48 && camPos.z > -72) {
            drawInteractiveFan(vec3(-4, 0, -60), 4.5f, 0.0f);
            drawInteractiveFan(vec3(4, 0, -60), 4.5f, 1.2f);
            drawAC(vec3(9.3f, 6.8f, -60));
            drawStairPulse(vec3(8.0f, 0, -56.2f));
        }
        if (abs(camPos.x - 30) < 12 && camPos.z < -45 && camPos.z > -66) {
            drawInteractiveFan(vec3(28.5f, 0, -55), 4.1f, 0.6f);
            drawInteractiveFan(vec3(31.5f, 0, -55), 4.1f, 2.0f);
            drawAC(vec3(36.2f, 6.2f, -55));
            drawStairPulse(vec3(35.5f, 0, -51.5f));
        }
    }

    // User-drawn curves
    s.setInt("texMode", 3);
    setMat(s, vec3(0.3f), vec3(0.9f, 0.2f, 0.5f), vec3(0.5f), 64, vec3(0.2f, 0.05f, 0.1f));
    for (int i = 0; i < (int)userCurves.size(); i++) {
        string name = "CustomObject" + to_string(i + 1);
        s.setMat4("model", makeSelectableModel(name));
        drawMesh(userCurves[i]);
    }
    s.setMat4("model", mat4(1.0f));

    // Control point visualization (curve edit mode)
    if (curveEditMode && !curveControlPoints.empty()) {
        s.setInt("texMode", 3);
        setMat(s, vec3(0.8f, 0.1f, 0.1f), vec3(0.9f, 0.15f, 0.15f), vec3(0.5f), 32, vec3(0.5f, 0.05f, 0.05f));
        for (auto& p : curveControlPoints) {
            mat4 pm = translate(mat4(1.0f), p); pm = scale(pm, vec3(0.15f));
            s.setMat4("model", pm); drawMesh(mProjectileBall);
        }
        s.setMat4("model", mat4(1.0f));
    }

    // Hierarchical Car
    if (drawCarModel) {
        static float wheelRotation = 0;
        wheelRotation += length(vec3(camera.Front.x, 0, camera.Front.z)) * deltaTime * 10.0f;
        vec3 carPos = camera.Position - vec3(0, 2.5f, 0);
        renderCar(s, carPos, camera.Yaw, wheelRotation);
        s.setMat4("model", mat4(1.0f));
    }

    // ======== RENDER ENTITIES ========
    float curTime = (float)glfwGetTime();

    // Birds
    s.setInt("texMode", 2);
    s.setBool("windEnabled", false);
    for (auto& bird : birds) {
        float yaw = bird.getYaw();
        mat4 birdRoot = translate(mat4(1.0f), bird.position);
        birdRoot = rotate(birdRoot, yaw, vec3(0, 1, 0));
        birdRoot = scale(birdRoot, vec3(bird.size));

        // Body
        s.setMat4("model", birdRoot);
        setMat(s, vec3(0.2f), vec3(0.35f, 0.3f, 0.3f), vec3(0.1f), 8, vec3(0));
        drawMesh(mBirdBody);

        // Left wing
        mat4 lwing = birdRoot;
        lwing = translate(lwing, vec3(0, 0, 0.12f));
        lwing = rotate(lwing, bird.wingAngle, vec3(1, 0, 0));
        s.setMat4("model", lwing);
        drawMesh(mBirdWing);

        // Right wing
        mat4 rwing = birdRoot;
        rwing = translate(rwing, vec3(0, 0, -0.12f));
        rwing = rotate(rwing, -bird.wingAngle, vec3(1, 0, 0));
        s.setMat4("model", rwing);
        drawMesh(mBirdWing);
    }

    // Students
    for (auto& stu : students) {
        float yaw = stu.getYaw();
        mat4 stuMat = translate(mat4(1.0f), stu.position);
        stuMat = rotate(stuMat, yaw, vec3(0, 1, 0));
        
        if (stu.isSitting()) {
            stuMat = translate(stuMat, vec3(0, -0.3f, 0)); // Lower down for sitting
            stuMat = scale(stuMat, vec3(1.0f, 0.6f, 1.0f)); // Compress legs to sit
        } else {
            // Walking animation: slight bob
            float bob = sin(stu.walkPhase) * 0.05f;
            stuMat = translate(stuMat, vec3(0, bob, 0));
        }
        
        s.setMat4("model", stuMat);
        setMat(s, vec3(0.2f), vec3(0.4f, 0.5f, 0.7f), vec3(0.1f), 16, vec3(0));
        drawMesh(mStudentFig);
    }

    // Projectiles
    s.setInt("texMode", 3);
    auto renderProj = [&](Projectile& proj, vec3 col) {
        if (!proj.active) return;
        mat4 pm = translate(mat4(1.0f), proj.position);
        s.setMat4("model", pm);
        setMat(s, vec3(0.3f), col, vec3(0.5f), 32, vec3(0));
        drawMesh(mProjectileBall);
    };
    renderProj(mainProjectile, vec3(0.9f, 0.1f, 0.1f));
    renderProj(cricketBall, vec3(0.85f, 0.15f, 0.1f));

    // Football projectile
    if (footballBall.active) {
        s.setInt("texMode", 2);
        mat4 fbm = translate(mat4(1.0f), footballBall.position);
        s.setMat4("model", fbm);
        setMat(s, vec3(0.3f), vec3(0.9f), vec3(0.3f), 16, vec3(0));
        drawMesh(mFootballMesh);
    }

    s.setMat4("model", mat4(1.0f));
}

// ======== RENDER SKY OBJECTS (Sun/Moon as textured spheres) ========
void renderSky(Shader& s, mat4 view, mat4 proj) {
    s.use();
    s.setMat4("view", view); s.setMat4("projection", proj);
    s.setBool("windEnabled", false);
    s.setBool("allOff", false);

    glDepthMask(GL_FALSE);

    if (dayNight.dirLightActive) {
        unsigned int sunTex = (dayNight.sunTexIdx == 0) ? texRedSun : texYellowSun;
        s.setInt("texMode", 4);
        glBindTexture(GL_TEXTURE_2D, sunTex);

        SelectableObj* sunSel = getSelectable("Sun");
        float sunObjectScale = sunSel ? sunSel->scl.x : 1.0f;
        float sunSz = 5.0f * dayNight.sunScale * sunObjectScale; // 20m diameter -> radius ~10m? Let's use 10
        vec3 sunPos = dayNight.sunPos + (sunSel ? sunSel->pos : vec3(0));
        
        mat4 sunModel = translate(mat4(1.0f), sunPos);
        // We draw the actual sphere now.
        sunModel = scale(sunModel, vec3(10.0f));
        
        s.setMat4("model", sunModel);
        setMat(s, vec3(0.5f), vec3(1.0f), vec3(0.3f), 32, vec3(0.5f, 0.4f, 0.2f));
        drawMesh(mSunSphere); // Use the 3D Textured Sphere!
    }

    if (dayNight.showMoon) {
        s.setInt("texMode", 4);
        glBindTexture(GL_TEXTURE_2D, texMoon);

        SelectableObj* moonSel = getSelectable("Moon");
        float moonObjectScale = moonSel ? moonSel->scl.x : 1.0f;
        float moonSz = 7.5f * moonObjectScale; // 15m diameter -> radius 7.5m
        vec3 moonPos = dayNight.moonPos + (moonSel ? moonSel->pos : vec3(0));
        
        mat4 moonModel = translate(mat4(1.0f), moonPos);
        moonModel = scale(moonModel, vec3(moonSz));
        
        s.setMat4("model", moonModel);
        float g = dayNight.moonGlitter;
        setMat(s, vec3(0.4f), vec3(0.9f), vec3(0.7f), 128, vec3(g * 0.8f, g * 0.9f, g * 1.0f));
        drawMesh(mMoonSphere);
    }

    glDepthMask(GL_TRUE);
    s.setMat4("model", mat4(1.0f));
}

// ======== RENDER CLOCK UI OVERLAY ========
void renderClockOverlay(Shader& s, int ww, int wh) {
    s.use();
    glDisable(GL_DEPTH_TEST);

    mat4 proj = ortho(0.0f, (float)ww, 0.0f, (float)wh, -10.0f, 10.0f);
    mat4 view(1.0f);
    s.setMat4("projection", proj);
    s.setMat4("view", view);
    s.setBool("windEnabled", false);
    s.setBool("allOff", true);
    s.setFloat("ambStr", 1.0f);
    s.setInt("texMode", 2); // vertex color

    float cx = ww - 70.f, cy = wh - 70.f, r = 50.f;

    // Clock face
    mat4 m = translate(mat4(1.0f), vec3(cx, cy, 0));
    m = scale(m, vec3(r, r, 1.0f));
    s.setMat4("model", m);
    setMat(s, vec3(1), vec3(1), vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    // Hour marks
    for (int i = 0; i < 12; i++) {
        float angle = (float)i / 12.f * 2.f * (float)M_PI;
        mat4 hm = translate(mat4(1.0f), vec3(cx + cos(angle) * r * 0.8f, cy + sin(angle) * r * 0.8f, 1));
        hm = scale(hm, vec3(4.f, 4.f, 1.f));
        s.setMat4("model", hm);
        setMat(s, vec3(0.3f), vec3(0.3f), vec3(0), 1, vec3(0));
        drawMesh(mClockFace); // small dot
    }

    // Get real time
    time_t rawtime; time(&rawtime);
    struct tm* ti = localtime(&rawtime);

    float secA = -(ti->tm_sec * 6.0f - 90.0f) * (float)M_PI / 180.0f;
    float minA = -(ti->tm_min * 6.0f + ti->tm_sec * 0.1f - 90.0f) * (float)M_PI / 180.0f;
    float hrA = -((ti->tm_hour % 12) * 30.0f + ti->tm_min * 0.5f - 90.0f) * (float)M_PI / 180.0f;

    // Second hand (long, thin, red)
    m = translate(mat4(1.0f), vec3(cx, cy, 2));
    m = rotate(m, secA, vec3(0, 0, 1));
    m = scale(m, vec3(r * 0.75f, 1.5f, 1.0f));
    s.setMat4("model", m);
    setMat(s, vec3(0.9f, 0.1f, 0.1f), vec3(0.9f, 0.1f, 0.1f), vec3(0), 1, vec3(0));
    drawMesh(mClockHand);

    // Minute hand (long, medium)
    m = translate(mat4(1.0f), vec3(cx, cy, 3));
    m = rotate(m, minA, vec3(0, 0, 1));
    m = scale(m, vec3(r * 0.65f, 3.0f, 1.0f));
    s.setMat4("model", m);
    setMat(s, vec3(0.2f), vec3(0.2f), vec3(0), 1, vec3(0));
    drawMesh(mClockHand);

    // Hour hand (short, thick)
    m = translate(mat4(1.0f), vec3(cx, cy, 4));
    m = rotate(m, hrA, vec3(0, 0, 1));
    m = scale(m, vec3(r * 0.45f, 4.0f, 1.0f));
    s.setMat4("model", m);
    setMat(s, vec3(0.15f), vec3(0.15f), vec3(0), 1, vec3(0));
    drawMesh(mClockHand);

    // Center dot
    m = translate(mat4(1.0f), vec3(cx, cy, 5));
    m = scale(m, vec3(5.f, 5.f, 1.f));
    s.setMat4("model", m);
    setMat(s, vec3(0.2f), vec3(0.2f), vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    // Phase indicator circle (top left corner)
    vec3 phaseCol;
    switch (dayNight.phase) {
    case PHASE_MORNING: phaseCol = vec3(0.9f, 0.5f, 0.2f); break;
    case PHASE_NOON: phaseCol = vec3(1.0f, 0.9f, 0.3f); break;
    case PHASE_EVENING: phaseCol = vec3(0.8f, 0.3f, 0.15f); break;
    case PHASE_NIGHT: phaseCol = vec3(0.2f, 0.2f, 0.5f); break;
    }
    m = translate(mat4(1.0f), vec3(30, (float)wh - 30, 0));
    m = scale(m, vec3(15.f, 15.f, 1.f));
    s.setMat4("model", m);
    setMat(s, phaseCol, phaseCol, vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    // Selection indicator
    if (selectionMode && selectedObjIdx >= 0 && selectedObjIdx < (int)selectables.size()) {
        m = translate(mat4(1.0f), vec3(30, 30, 0));
        m = scale(m, vec3(12.f, 12.f, 1.f));
        s.setMat4("model", m);
        setMat(s, vec3(0.1f, 0.9f, 0.3f), vec3(0.1f, 0.9f, 0.3f), vec3(0), 1, vec3(0));
        drawMesh(mClockFace);
    }

    // UI status indicators: wireframe/light/camera mode
    vec3 wfCol = wireframeMode ? vec3(0.95f, 0.55f, 0.15f) : vec3(0.25f, 0.25f, 0.25f);
    m = translate(mat4(1.0f), vec3(60, 30, 0));
    m = scale(m, vec3(10.f, 10.f, 1.f));
    s.setMat4("model", m);
    setMat(s, wfCol, wfCol, vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    vec3 lightCol = allLightsOff ? vec3(0.3f, 0.1f, 0.1f) : vec3(0.15f, 0.7f, 0.2f);
    m = translate(mat4(1.0f), vec3(90, 30, 0));
    m = scale(m, vec3(10.f, 10.f, 1.f));
    s.setMat4("model", m);
    setMat(s, lightCol, lightCol, vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    vec3 camModeCol = useFreeCamera ? vec3(0.25f, 0.55f, 0.95f) : vec3(0.6f, 0.45f, 0.2f);
    m = translate(mat4(1.0f), vec3(120, 30, 0));
    m = scale(m, vec3(10.f, 10.f, 1.f));
    s.setMat4("model", m);
    setMat(s, camModeCol, camModeCol, vec3(0), 1, vec3(0));
    drawMesh(mClockFace);

    s.setBool("allOff", allLightsOff);
    s.setFloat("ambStr", ambStr);
    s.setMat4("model", mat4(1.0f));
    glEnable(GL_DEPTH_TEST);
}

void renderUI(Shader& textShader, int ww, int wh) {
    mat4 orthoProj = ortho(0.0f, (float)ww, (float)wh, 0.0f, -1.0f, 1.0f);
    textShader.use();
    textShader.setMat4("projection", orthoProj);

    // Map UI
    if (showMap) {
        // Just text for now as drawing map shapes needs another shader or quad generator.
        renderText(textShader, "++ CAMPUS MAP ++", ww/2 - 100, 100, 1.2f, vec3(0.2f, 0.8f, 0.2f));
        renderText(textShader, "Academic Bldg [Center North]", ww/2 - 120, 150, 0.8f, vec3(1));
        renderText(textShader, "Hall Bldg [East]", ww/2 - 120, 180, 0.8f, vec3(1));
        renderText(textShader, "Dept Bldg [West]", ww/2 - 120, 210, 0.8f, vec3(1));
        renderText(textShader, "Library [South West]", ww/2 - 120, 240, 0.8f, vec3(1));
        renderText(textShader, "SWC [South East]", ww/2 - 120, 270, 0.8f, vec3(1));
        
        vec3 camPos = useFreeCamera ? freeCamera.Position : camera.Position;
        renderText(textShader, "YOU ARE HERE: " + to_string((int)camPos.x) + ", " + to_string((int)camPos.z), ww/2 - 110, 310, 1.0f, vec3(0.9f, 0.1f, 0.1f));
    }

    // Directory UI
    if (showDirectory) {
        renderText(textShader, "== BUILDING DIRECTORY ==", ww/2 - 130, 100, 1.2f, vec3(0.2f, 0.6f, 0.9f));
        renderText(textShader, "Academic: 2 Floors, 8 Classrooms", ww/2 - 120, 150, 0.8f, vec3(1));
        renderText(textShader, "Hall: 2 Floors, 8 Rooms", ww/2 - 120, 180, 0.8f, vec3(1));
        renderText(textShader, "Dept: 2 Floors, Offices", ww/2 - 120, 210, 0.8f, vec3(1));
    }

    // Legend UI
    if (showLegend) {
        renderText(textShader, "CONTROLS:", 30, wh/2 - 100, 1.0f, vec3(0.9f, 0.8f, 0.2f));
        renderText(textShader, "W/A/S/D - Move", 30, wh/2 - 70, 0.7f, vec3(1));
        renderText(textShader, "SPACE - Interact with nearby props", 30, wh/2 - 50, 0.7f, vec3(1));
        renderText(textShader, "M - Toggle Map", 30, wh/2 - 30, 0.7f, vec3(1));
        renderText(textShader, "D - Toggle Directory", 30, wh/2 - 10, 0.7f, vec3(1));
        renderText(textShader, "? - Toggle Controls Legend", 30, wh/2 + 10, 0.7f, vec3(1));
        renderText(textShader, "TAB - Selection Mode (Y/P/R to rotate)", 30, wh/2 + 30, 0.7f, vec3(1));
    }

    // Context Prompts (e.g. nearby interactable)
    vec3 camPos = useFreeCamera ? freeCamera.Position : camera.Position;
    bool nearProp = false;
    for (auto& prop : interactiveProps) {
        if (length(prop.position - camPos) < 5.0f) {
            nearProp = true; break;
        }
    }
    if (nearProp && !showMap && !showDirectory) {
        renderText(textShader, "[Press Space to interact]", ww/2 - 100, wh - 100, 1.0f, vec3(1.0f, 0.9f, 0.1f));
    }

    // Building Intro
    if (buildingIntroTimer > 0) {
        renderText(textShader, "Entering: " + buildingIntroText, ww/2 - 150, wh/2, 1.5f, vec3(1.0f, 1.0f, 1.0f) * std::min(1.0f, buildingIntroTimer));
    }
}

// ======== SCREEN TO GROUND RAY CAST ========
vec3 screenToGround(double mx, double my, mat4 view, mat4 proj, int w, int h) {
    float x = (2.0f * (float)mx) / w - 1.0f;
    float y = 1.0f - (2.0f * (float)my) / h;
    mat4 inv = inverse(proj * view);
    vec4 nearPt = inv * vec4(x, y, -1, 1); nearPt /= nearPt.w;
    vec4 farPt = inv * vec4(x, y, 1, 1); farPt /= farPt.w;
    vec3 dir = normalize(vec3(farPt) - vec3(nearPt));
    vec3 orig = vec3(nearPt);
    if (abs(dir.y) < 0.0001f) return vec3(0);
    float t = -orig.y / dir.y;
    return orig + dir * t;
}

// ======== CALLBACKS ========
void fbCB(GLFWwindow*, int w, int h) { }

void mouseCB(GLFWwindow* window, double xpos, double ypos) {
    float xp = (float)xpos, yp = (float)ypos;
    if (firstMouse) { lastX = xp; lastY = yp; firstMouse = false; }

    if (curveEditMode || selectionMode) return; // Don't rotate camera in edit modes

    if (useFreeCamera) {
        freeCamera.ProcessMouseMovement(xp - lastX, lastY - yp);
    }
    else if (birdEyeView) {
        // Bird's eye: orbit
    }
    else {
        camera.ProcessMouseMovement(xp - lastX, lastY - yp);
    }
    lastX = xp; lastY = yp;
}

void scrollCB(GLFWwindow*, double, double y) {
    if (birdEyeView) {
        freeCamera.Position.y += (float)y * 5.0f;
        freeCamera.Position.y = clamp(freeCamera.Position.y, 30.0f, 200.0f);
        return;
    }
    if (useFreeCamera) freeCamera.ProcessMouseScroll((float)y);
    else camera.ProcessMouseScroll((float)y);
}

void mouseButtonCB(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        int ww, wh;
        glfwGetFramebufferSize(window, &ww, &wh);
        mat4 proj = perspective(radians(45.0f), (float)ww / wh, 0.1f, 300.0f);
        mat4 vw = useFreeCamera ? freeCamera.GetViewMatrix() : camera.GetViewMatrix();

        if (curveEditMode) {
            vec3 worldPt = screenToGround(mx, my, vw, proj, ww, wh);
            worldPt.y = 0.5f; // Slight elevation
            curveControlPoints.push_back(worldPt);
            cout << "Control point added: (" << worldPt.x << ", " << worldPt.y << ", " << worldPt.z << ") Total: " << curveControlPoints.size() << endl;
        } else if (!selectionMode) {
            // NPC Raycasting interaction
            vec3 activePos = useFreeCamera ? freeCamera.Position : camera.Position;
            vec3 pt = screenToGround(mx, my, vw, proj, ww, wh);
            vec3 rayDir = normalize(pt - activePos);
            
            float bestDist = 1000.0f; int bestNPC = -1;
            for (int i=0; i<(int)students.size(); i++) {
                vec3 oc = activePos - (students[i].position + vec3(0, 0.6f, 0)); // npc center ~ 0.6y
                float b = dot(oc, rayDir);
                float c = dot(oc, oc) - 0.4f*0.4f; // interaction radius 0.4
                if (b*b - c > 0) {
                    float t = -b - sqrt(b*b - c);
                    if (t > 0 && t < bestDist && t < 20.0f) { bestDist = t; bestNPC = i; }
                }
            }
            if (bestNPC != -1) {
                static int diagState = 0;
                cout << "\n>>> CONVERSATION WITH STUDENT MALE_" << bestNPC << " <<<" << endl;
                if (diagState % 2 == 0) {
                    cout << "You: What is your name?" << endl;
                    cout << "NPC: I am John Doe." << endl;
                } else {
                    cout << "You: Which department are you from?" << endl;
                    cout << "NPC: CSE 2K20, sir." << endl;
                }
                diagState++;
                cout << "---------------------------------------" << endl;
            }
        }
    }
}

void keyCB(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, true);
    if (key == GLFW_KEY_V) fourVP = !fourVP;
    if (key == GLFW_KEY_1) activeVP = 1;
    if (key == GLFW_KEY_2) activeVP = 2;
    if (key == GLFW_KEY_3) activeVP = 3;
    if (key == GLFW_KEY_4) activeVP = 4;
    if (key == GLFW_KEY_P) usePhong = true;
    if (key == GLFW_KEY_G) usePhong = false;
    if (key == GLFW_KEY_L) allLightsOff = !allLightsOff;
    if (key == GLFW_KEY_N) manualNightMode = !manualNightMode;
    if (key == GLFW_KEY_E) emissiveOn = !emissiveOn;
    if (key == GLFW_KEY_B) showBez = !showBez;
    if (key == GLFW_KEY_T) demoTexMode = (demoTexMode + 1) % 4;
    if (key == GLFW_KEY_F1) {
        wireframeMode = !wireframeMode;
        cout << (wireframeMode ? "Wireframe ON" : "Wireframe OFF") << endl;
    }
    if (key == GLFW_KEY_F2) {
        interiorFansOn = !interiorFansOn;
        cout << (interiorFansOn ? "Interior fans ON" : "Interior fans OFF") << endl;
    }
    if (key == GLFW_KEY_F3) {
        interiorACOn = !interiorACOn;
        cout << (interiorACOn ? "Interior AC ON" : "Interior AC OFF") << endl;
    }
    if (key == GLFW_KEY_F4) {
        interiorLightsOn = !interiorLightsOn;
        cout << (interiorLightsOn ? "Interior lights ON" : "Interior lights OFF") << endl;
    }
    if (key == GLFW_KEY_F5) {
        interiorStairsOn = !interiorStairsOn;
        cout << (interiorStairsOn ? "Stairs interaction ON" : "Stairs interaction OFF") << endl;
    }
    if (!selectionMode) {
        if (key == GLFW_KEY_EQUAL) ambStr = std::min(1.0f, ambStr + 0.05f);
        if (key == GLFW_KEY_MINUS) ambStr = std::max(0.0f, ambStr - 0.05f);
    }
    if (key == GLFW_KEY_5) litOn[0] = !litOn[0];
    if (key == GLFW_KEY_6) litOn[1] = !litOn[1];
    if (key == GLFW_KEY_7) litOn[2] = !litOn[2];
    if (key == GLFW_KEY_8) litOn[3] = !litOn[3];
    if (key == GLFW_KEY_9) litOn[4] = !litOn[4];
    if (key == GLFW_KEY_0) litOn[5] = !litOn[5];
    if (key == GLFW_KEY_U) dirLightOn = !dirLightOn;
    if (key == GLFW_KEY_I) spotLightOn = !spotLightOn;

    // Free camera toggle
    if (key == GLFW_KEY_F) {
        useFreeCamera = !useFreeCamera;
        birdEyeView = false;
        if (useFreeCamera) {
            freeCamera.groundClamped = false;
            cout << "Free Camera ON" << endl;
        }
        else {
            cout << "Car Camera ON" << endl;
        }
    }

    // Bird's eye view
    if (key == GLFW_KEY_O) {
        birdEyeView = !birdEyeView;
        if (birdEyeView) {
            useFreeCamera = true;
            freeCamera.Position = vec3(0, 120, 0);
            freeCamera.Pitch = -89.0f;
            freeCamera.Yaw = -90.0f;
            freeCamera.groundClamped = false;
            freeCamera.ProcessMouseMovement(0, 0); // Update vectors
            cout << "Bird's Eye View ON" << endl;
        }
        else {
            cout << "Bird's Eye View OFF" << endl;
        }
    }

    // Cricket simulation
    if (key == GLFW_KEY_C) {
        cricketBall.launch(vec3(44, 1.5f, 33.5f), vec3(0, 3.0f, 2.5f));
        cout << "Cricket ball bowled!" << endl;
    }

    // Football simulation
    if (key == GLFW_KEY_X) {
        footballBall.launch(vec3(-52, 0.3f, -28), vec3(0, 4.0f, -6.0f));
        cout << "Football kicked!" << endl;
    }

    // Throw ball from building (physics)
    if (key == GLFW_KEY_J) {
        vec3 launchPos = (useFreeCamera ? freeCamera.Position : camera.Position) + vec3(0, 2, 0);
        vec3 launchDir = (useFreeCamera ? freeCamera.Front : camera.Front);
        mainProjectile.launch(launchPos, launchDir * 10.0f + vec3(0, 5, 0));
        cout << "Ball thrown!" << endl;
    }

    // Curve editing mode
    if (key == GLFW_KEY_K) {
        curveEditMode = !curveEditMode;
        if (curveEditMode) {
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            curveControlPoints.clear();
            cout << "Curve Edit Mode ON - Click to place control points, Enter to confirm, M to toggle Bezier/B-spline, Delete to clear" << endl;
        }
        else {
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true;
            cout << "Curve Edit Mode OFF" << endl;
        }
    }

    // Toggle curve type
    if (key == GLFW_KEY_M && curveEditMode) {
        useBSpline = !useBSpline;
        cout << (useBSpline ? "B-Spline" : "Bezier") << " curve mode" << endl;
    }

    if (key == GLFW_KEY_BACKSPACE && curveEditMode) {
        if (!curveControlPoints.empty()) {
            curveControlPoints.pop_back();
            cout << "Last control point removed. Remaining: " << curveControlPoints.size() << endl;
        }
    }

    // Confirm curve
    if (key == GLFW_KEY_ENTER && curveEditMode) {
        if (curveControlPoints.size() >= 4) {
            vector<float> cv;
            if (useBSpline) {
                vector<vec2> profile;
                float sps = 12;
                for (int i = 0; i < (int)curveControlPoints.size() - 3; i++) {
                    for (int j = 0; j < sps; j++) {
                        float t = (float)j / sps;
                        vec3 pt = bsplinePt(curveControlPoints[i], curveControlPoints[i + 1], curveControlPoints[i + 2], curveControlPoints[i + 3], t);
                        profile.push_back(vec2(pt.x, pt.y));
                    }
                }
                vec3 ptLast = bsplinePt(curveControlPoints[curveControlPoints.size() - 4], curveControlPoints[curveControlPoints.size() - 3], curveControlPoints[curveControlPoints.size() - 2], curveControlPoints[curveControlPoints.size() - 1], 1.0f);
                profile.push_back(vec2(ptLast.x, ptLast.y));

                float dtheta = 2.f * (float)M_PI / 24;
                for (int i = 0; i < (int)profile.size() - 1; i++) for (int j = 0; j < 24; j++) {
                    float t1 = j * dtheta, t2 = (j + 1) * dtheta;
                    float r1 = profile[i].x, y1 = profile[i].y, r2 = profile[i + 1].x, y2 = profile[i + 1].y;
                    vec3 a(r1 * cos(t1), y1, -r1 * sin(t1)), b(r1 * cos(t2), y1, -r1 * sin(t2));
                    vec3 c(r2 * cos(t1), y2, -r2 * sin(t1)), d(r2 * cos(t2), y2, -r2 * sin(t2));
                    vec3 na = normalize(vec3(cos(t1), 0, -sin(t1))), nb = normalize(vec3(cos(t2), 0, -sin(t2)));
                    float u1 = (float)j / 24, u2 = (float)(j + 1) / 24, v1 = (float)i / profile.size(), v2 = (float)(i + 1) / profile.size();
                    vec3 col(0.9f, 0.2f, 0.5f);
                    pv(cv, a, na, u1, v1, col); pv(cv, c, na, u1, v2, col); pv(cv, b, nb, u2, v1, col);
                    pv(cv, b, nb, u2, v1, col); pv(cv, c, na, u1, v2, col); pv(cv, d, nb, u2, v2, col);
                }
            }
            else {
                // Bezier: use hollow bezier (surface of revolution)
                genHollowBezier(cv, curveControlPoints, vec3(0.9f, 0.2f, 0.5f), 30, 24);
            }
            if (!cv.empty()) {
                userCurves.push_back(makeMesh(cv));
                string name = "CustomObject" + to_string(userCurves.size());
                selectables.push_back({ name, vec3(0), vec3(0), vec3(1), 20 + (int)userCurves.size() });
                cout << "Curve saved! Created " << name << ". Total curves: " << userCurves.size() << endl;
            }
            curveControlPoints.clear();
        }
        else {
            cout << "Need at least 4 control points!" << endl;
        }
    }

    // Delete curves
    if (key == GLFW_KEY_DELETE) {
        userCurves.clear();
        curveControlPoints.clear();
        cout << "All user curves cleared" << endl;
    }

    // Object selection mode
    if (key == GLFW_KEY_TAB) {
        selectionMode = !selectionMode;
        if (selectionMode) {
            selectedObjIdx = 0;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            cout << "Selection Mode ON - [/] cycle, Arrow keys move, +/- scale, R rotate" << endl;
            if (!selectables.empty()) cout << "Selected: " << selectables[0].name << endl;
        }
        else {
            selectedObjIdx = -1;
            if (!curveEditMode) {
                glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                firstMouse = true;
            }
            cout << "Selection Mode OFF" << endl;
        }
    }

    // Cycle selection
    if (selectionMode && (key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_PERIOD)) {
        selectedObjIdx = (selectedObjIdx + 1) % (int)selectables.size();
        cout << "Selected: " << selectables[selectedObjIdx].name << endl;
    }
    if (selectionMode && (key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_COMMA)) {
        selectedObjIdx = (selectedObjIdx - 1 + (int)selectables.size()) % (int)selectables.size();
        cout << "Selected: " << selectables[selectedObjIdx].name << endl;
    }

    // Scale selected object
    if (selectionMode && selectedObjIdx >= 0) {
        if (key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL) {
            selectables[selectedObjIdx].scl *= 1.1f;
            cout << selectables[selectedObjIdx].name << " scaled up" << endl;
        }
        if (key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS) {
            selectables[selectedObjIdx].scl *= 0.9f;
            cout << selectables[selectedObjIdx].name << " scaled down" << endl;
        }
        if (key == GLFW_KEY_Y) {
            selectables[selectedObjIdx].rot.y += 0.3f;
            cout << selectables[selectedObjIdx].name << " rotated Yaw" << endl;
        }
        if (key == GLFW_KEY_P) {
            selectables[selectedObjIdx].rot.x += 0.3f;
            cout << selectables[selectedObjIdx].name << " rotated Pitch" << endl;
        }
        if (key == GLFW_KEY_R) {
            selectables[selectedObjIdx].rot.z += 0.3f;
            cout << selectables[selectedObjIdx].name << " rotated Roll" << endl;
        }
    }

    // Auto tour
    if (key == GLFW_KEY_H) {
        autoTour = !autoTour;
        autoTourT = 0;
        if (autoTour) {
            useFreeCamera = true;
            freeCamera.groundClamped = false;
            cout << "Auto Tour ON" << endl;
        }
        else cout << "Auto Tour OFF" << endl;
    }

    // Day/night speed controls
    if (key == GLFW_KEY_Z) {
        dayNight.speed *= 2.0f;
        dayNight.speed = std::min(dayNight.speed, 16.0f);
        cout << "Day/Night speed: " << dayNight.speed << "x" << endl;
    }
    if (key == GLFW_KEY_Y && !selectionMode) {
        dayNight.speed *= 0.5f;
        dayNight.speed = std::max(dayNight.speed, 0.125f);
        cout << "Day/Night speed: " << dayNight.speed << "x" << endl;
    }
    
    // Space to interact
    if (key == GLFW_KEY_SPACE) {
        vec3 camPos = useFreeCamera ? freeCamera.Position : camera.Position;
        for (auto& prop : interactiveProps) {
            if (length(prop.position - camPos) < 5.0f) {
                prop.active = !prop.active;
                cout << "Toggled prop of type " << prop.type << " at distance " << length(prop.position - camPos) << endl;
            }
        }
    }
    // UI toggles
    if (key == GLFW_KEY_M) showMap = !showMap;
    if (key == GLFW_KEY_D) showDirectory = !showDirectory;
    if (key == GLFW_KEY_SLASH && (mods & GLFW_MOD_SHIFT)) showLegend = !showLegend;
}

void processInput(GLFWwindow* w) {
    if (curveEditMode || selectionMode) {
        // Arrow keys for moving selected object
        if (selectionMode && selectedObjIdx >= 0) {
            float mv = 0.5f;
            if (glfwGetKey(w, GLFW_KEY_UP) == GLFW_PRESS) selectables[selectedObjIdx].pos.z -= mv * deltaTime * 10;
            if (glfwGetKey(w, GLFW_KEY_DOWN) == GLFW_PRESS) selectables[selectedObjIdx].pos.z += mv * deltaTime * 10;
            if (glfwGetKey(w, GLFW_KEY_LEFT) == GLFW_PRESS) selectables[selectedObjIdx].pos.x -= mv * deltaTime * 10;
            if (glfwGetKey(w, GLFW_KEY_RIGHT) == GLFW_PRESS) selectables[selectedObjIdx].pos.x += mv * deltaTime * 10;
            if (glfwGetKey(w, GLFW_KEY_PAGE_UP) == GLFW_PRESS) selectables[selectedObjIdx].pos.y += mv * deltaTime * 8;
            if (glfwGetKey(w, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) selectables[selectedObjIdx].pos.y -= mv * deltaTime * 8;
        }
        return;
    }

    if (autoTour) return; // Don't allow manual movement during tour

    if (useFreeCamera) {
        if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) freeCamera.ProcessKeyboard(CAM_FORWARD, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) freeCamera.ProcessKeyboard(CAM_BACKWARD, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) freeCamera.ProcessKeyboard(CAM_LEFT, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) freeCamera.ProcessKeyboard(CAM_RIGHT, deltaTime);
    }
    else {
        // Car camera with collision detection
        vec3 oldPos = camera.Position;
        if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(CAM_FORWARD, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(CAM_BACKWARD, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(CAM_LEFT, deltaTime);
        if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(CAM_RIGHT, deltaTime);

        // Check collision and resolve
        vec3 newPos = camera.Position;
        camera.Position = resolveCollision(oldPos, newPos, colliders, 1.2f);
        camera.Position.y = camera.groundY; // Always clamp to ground
    }
}

// ======== MAIN ========
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SW, SH, "KUET Campus Explorer - Full Feature", NULL, NULL);
    if (!window) { cout << "Window creation failed" << endl; glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, fbCB);
    glfwSetCursorPosCallback(window, mouseCB);
    glfwSetScrollCallback(window, scrollCB);
    glfwSetMouseButtonCallback(window, mouseButtonCB);
    glfwSetKeyCallback(window, keyCB);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { cout << "GLAD loading failed" << endl; return -1; }

    glEnable(GL_DEPTH_TEST);

    Shader phong(phongVS, phongFS);
    Shader gouraud(gouraudVS, gouraudFS);
    Shader textShader(textVS, textFS);
    initText();

    setupScene();

    cout << "============================================" << endl;
    cout << "  KUET Campus Explorer - Full Feature" << endl;
    cout << "============================================" << endl;
    cout << "CAMERA:" << endl;
    cout << "  W/A/S/D   - Move (car stays on ground)" << endl;
    cout << "  Mouse     - Look around" << endl;
    cout << "  Scroll    - Zoom" << endl;
    cout << "  F         - Toggle free camera (fly mode)" << endl;
    cout << "  O         - Bird's eye view" << endl;
    cout << "  H         - Auto camera tour" << endl;
    cout << "LIGHTING:" << endl;
    cout << "  P/G       - Phong/Gouraud shading" << endl;
    cout << "  L         - Toggle all lights off" << endl;
    cout << "  U         - Toggle directional (sun) light" << endl;
    cout << "  I         - Toggle spotlight" << endl;
    cout << "  5-0       - Toggle point lights" << endl;
    cout << "  N         - Manual night mode" << endl;
    cout << "  E         - Toggle emissive glow" << endl;
    cout << "  +/-       - Adjust ambient" << endl;
    cout << "DAY/NIGHT:" << endl;
    cout << "  Z/Y       - Speed up/slow down cycle" << endl;
    cout << "SIMULATION:" << endl;
    cout << "  C         - Bowl cricket ball" << endl;
    cout << "  X         - Kick football" << endl;
    cout << "  J         - Throw ball (gravity)" << endl;
    cout << "CURVES:" << endl;
    cout << "  K         - Curve edit mode (click to place)" << endl;
    cout << "  M         - Toggle Bezier/B-spline" << endl;
    cout << "  Enter     - Confirm curve" << endl;
    cout << "  Backspace - Remove last control point" << endl;
    cout << "  Delete    - Clear curves" << endl;
    cout << "SELECTION:" << endl;
    cout << "  Tab       - Object selection mode" << endl;
    cout << "  [/] or ,/. - Cycle objects" << endl;
    cout << "  Arrows    - Translate selected" << endl;
    cout << "  R         - Rotate selected" << endl;
    cout << "  Numpad+/- - Scale selected" << endl;
    cout << "  PgUp/PgDn - Vertical translate selected" << endl;
    cout << "VIEW:" << endl;
    cout << "  V         - Toggle 4-viewport" << endl;
    cout << "  1-4       - Select viewport" << endl;
    cout << "  B         - Show/hide Bezier arch" << endl;
    cout << "  T         - Cycle texture modes" << endl;
    cout << "  F1        - Toggle wireframe" << endl;
    cout << "  F2/F3/F4/F5 - Toggle interior Fan/AC/Light/Stair interactions" << endl;
    cout << "  ESC       - Exit" << endl;
    cout << "============================================" << endl;

    // Auto tour waypoints (Catmull-Rom)
    vector<vec3> tourPts = {
        vec3(0, 15, 70), vec3(-30, 12, 50), vec3(-50, 10, 20), vec3(-45, 12, 10),
        vec3(-40, 15, -20), vec3(-20, 12, -50), vec3(0, 15, -65), vec3(30, 12, -55),
        vec3(40, 15, -30), vec3(50, 12, 0), vec3(40, 15, 20), vec3(30, 12, 30),
        vec3(10, 15, 40), vec3(0, 15, 70)
    };
    tourPts.insert(tourPts.begin(), tourPts.back());
    tourPts.push_back(tourPts[2]);

    while (!glfwWindowShouldClose(window)) {
        float ct = (float)glfwGetTime();
        deltaTime = ct - lastFrame; lastFrame = ct;
        deltaTime = std::min(deltaTime, 0.05f); // Cap delta

        processInput(window);

        // ======== UPDATE ========
        // Day/night cycle
        if (!manualNightMode) dayNight.update(deltaTime);

        // Wind
        windDir = vec3(sin(ct * 0.5f) * 2.0f, 0, cos(ct * 0.7f) * 1.5f);

        // Entities + schedule
        for (auto& stu : students) stu.updateSchedule(dayNight.phase, deltaTime);
        for (auto& bird : birds) bird.update(deltaTime, ct, windDir);
        for (auto& stu : students) stu.update(deltaTime);

        // Functional door behavior with schedule and proximity
        bool studentAtAcademicDoor = false;
        // Update global interactive props
        for(auto& p : interactiveProps) p.update(deltaTime);

        // Projectiles
        mainProjectile.update(deltaTime);
        cricketBall.update(deltaTime);
        footballBall.update(deltaTime);

        // Building Intro Logic
        vec3 camPos = useFreeCamera ? freeCamera.Position : camera.Position;
        if (length(camPos - vec3(0, 0, -57.0f)) < 15.0f && buildingIntroText != "Academic Building") {
            buildingIntroText = "Academic Building"; buildingIntroTimer = 5.0f;
        } else if (length(camPos - vec3(30, 0, -52.0f)) < 15.0f && buildingIntroText != "Hall Building") {
            buildingIntroText = "Hall Building"; buildingIntroTimer = 5.0f;
        } else if (length(camPos - vec3(-30, 0, -42.0f)) < 15.0f && buildingIntroText != "Department Building") {
            buildingIntroText = "Department Building"; buildingIntroTimer = 5.0f;
        }
        if (buildingIntroTimer > 0) buildingIntroTimer -= deltaTime;

        // Auto tour
        if (autoTour) {
            autoTourT += deltaTime * 0.03f;
            if (autoTourT > 1.0f) autoTourT -= 1.0f;
            int totalSeg = (int)tourPts.size() - 3;
            float globalT = autoTourT * totalSeg;
            int seg = (int)globalT;
            float localT = globalT - seg;
            if (seg < 0) seg = 0;
            if (seg >= totalSeg) seg = totalSeg - 1;
            vec3 tourPos = cmr(tourPts[seg], tourPts[seg + 1], tourPts[seg + 2], tourPts[seg + 3], localT);
            vec3 tourNext = cmr(tourPts[seg], tourPts[seg + 1], tourPts[seg + 2], tourPts[seg + 3], localT + 0.01f);
            freeCamera.Position = tourPos;
            vec3 lookDir = normalize(tourNext - tourPos);
            freeCamera.Yaw = degrees(atan2(lookDir.z, lookDir.x));
            freeCamera.Pitch = degrees(asin(lookDir.y));
            freeCamera.ProcessMouseMovement(0, 0); // Update vectors
        }

        // Night mode ambient
        if (manualNightMode) {
            ambStr = 0.05f;
            dayNight.skyColor = vec3(0.03f, 0.03f, 0.1f);
            dayNight.dirLightActive = false;
            dayNight.showMoon = true;
            dayNight.sunColor = vec3(0);
        }

        // ======== RENDER ========
        glClearColor(dayNight.skyColor.x, dayNight.skyColor.y, dayNight.skyColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, wireframeMode ? GL_LINE : GL_FILL);
        Shader& sh = usePhong ? phong : gouraud;
        int ww, wh; glfwGetFramebufferSize(window, &ww, &wh);
        float aspect = (float)ww / wh;

        if (fourVP) {
            int hw = ww / 2, hh = wh / 2;
            mat4 proj = perspective(radians(45.0f), (float)hw / hh, 0.1f, 300.0f);

            // Top-left: front view
            glViewport(0, hh, hw, hh); glScissor(0, hh, hw, hh); glEnable(GL_SCISSOR_TEST);
            glClear(GL_DEPTH_BUFFER_BIT);
            mat4 fv = lookAt(vec3(0, 5, 70), vec3(0, 3, -30), vec3(0, 1, 0));
            renderScene(sh, fv, proj, true);
            renderSky(sh, fv, proj);

            // Top-right: top view (bird's eye)
            glViewport(hw, hh, hw, hh); glScissor(hw, hh, hw, hh);
            glClear(GL_DEPTH_BUFFER_BIT);
            mat4 tv = lookAt(vec3(0, 100, 0), vec3(0, 0, 0), vec3(0, 0, -1));
            renderScene(sh, tv, proj, true);

            // Bottom-left: side view
            glViewport(0, 0, hw, hh); glScissor(0, 0, hw, hh);
            glClear(GL_DEPTH_BUFFER_BIT);
            mat4 sv = lookAt(vec3(90, 15, 0), vec3(0, 3, 0), vec3(0, 1, 0));
            renderScene(sh, sv, proj, true);
            renderSky(sh, sv, proj);

            // Bottom-right: active camera
            glViewport(hw, 0, hw, hh); glScissor(hw, 0, hw, hh);
            glClear(GL_DEPTH_BUFFER_BIT);
            mat4 camView = useFreeCamera ? freeCamera.GetViewMatrix() : camera.GetViewMatrix();
            renderScene(sh, camView, proj, !useFreeCamera);
            renderSky(sh, camView, proj);

            glDisable(GL_SCISSOR_TEST);

            // Clock overlay on full window
            glViewport(0, 0, ww, wh);
            renderClockOverlay(sh, ww, wh);
        }
        else {
            glViewport(0, 0, ww, wh);
            float zoom = useFreeCamera ? freeCamera.Zoom : camera.Zoom;
            mat4 proj = perspective(radians(zoom), aspect, 0.1f, 300.0f);
            mat4 vw;
            bool dv = true;
            if (activeVP == 1) vw = lookAt(vec3(0, 5, 70), vec3(0, 3, -30), vec3(0, 1, 0));
            else if (activeVP == 2) vw = lookAt(vec3(0, 100, 0), vec3(0, 0, 0), vec3(0, 0, -1));
            else if (activeVP == 3) vw = lookAt(vec3(90, 15, 0), vec3(0, 3, 0), vec3(0, 1, 0));
            else { vw = useFreeCamera ? freeCamera.GetViewMatrix() : camera.GetViewMatrix(); dv = !useFreeCamera; }
            renderScene(sh, vw, proj, dv);
            renderSky(sh, vw, proj);
            renderClockOverlay(sh, ww, wh);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        renderUI(textShader, ww, wh);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}