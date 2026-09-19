#pragma once

#include "meshgen.h"
#include "game.h"
#include <string>

constexpr int CHARACTER_BONES = 16;
struct CharacterAsset {
    MeshData mesh;
    std::vector<unsigned char> pixels;
    std::vector<unsigned char> normals, material;
    int detailWidth = 0, detailHeight = 0;
    int width = 0, height = 0;
    Vector3 restA[CHARACTER_BONES]{}, restB[CHARACTER_BONES]{};
};

bool LoadCharacterAsset(const std::string &path, const std::string &texture,
                        int character, CharacterAsset &out);
void CharacterPose(const CharacterAsset &asset, const Fighter &fighter, Matrix *bones);
bool CharactersInit(const char *assetDirectory);
bool CharacterDraw(const Fighter &fighter, bool shadow, const Matrix &shadowProjection);
void CharactersShutdown();
