#pragma once
#include <cmath>
enum class ShotKind { Cut, Bounce, Direct };
inline ShotKind shotKind(unsigned roll) {
    return roll%100<30?ShotKind::Cut:roll%100<55?ShotKind::Bounce:ShotKind::Direct;
}
struct Vec { float x,y,z; };
inline Vec operator-(Vec a,Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec operator*(Vec a,float b) { return {a.x*b,a.y*b,a.z*b}; }
inline float dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float length(Vec a) { return std::sqrt(dot(a,a)); }
struct CutRedirect {
    enum Result { Flying, Slap, Cancel };
    bool armed=false;
    Vec point{},expected{};
    void begin(Vec p,Vec velocity) {point=p;expected=velocity;armed=true;}
    Result update(Vec position,Vec velocity,bool held) {
        if(!armed) return Cancel;
        float speed=length(expected),distance=length(point-position);
        // A catch, collision or player slap ends our authority over this pass.
        if(held || !std::isfinite(distance+length(velocity)) || speed<1 ||
           length(velocity-expected)>speed*.08f) {armed=false;return Cancel;}
        Vec offset=position-point,axis=expected*(1.f/speed);
        float along=dot(offset,axis);
        if(length(offset-axis*along)>.35f || along>.5f) {armed=false;return Cancel;}
        if(distance<=.5f) {armed=false;return Slap;}
        return Flying;
    }
};
