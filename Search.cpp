/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "MapReduceFramework.h"
#include <dirent.h>
#include <string>
#include <iostream>
#include <vector>

using namespace std;

//input key and value.
//the key for the map function and the MapReduceFramework
class FolderName : public k1Base {
public:
    string _folderName;

    FolderName(string folderName);
    bool operator<(const k1Base &other) const;
    ~FolderName();

};
/**
 * constructor for the FolderName class
 * @param folderName
 */
FolderName::FolderName(string folderName) : _folderName(folderName) {}

FolderName::~FolderName() {}
bool FolderName::operator <(const k1Base& other) const {
    FolderName* f = (FolderName*)&other;
    return this->_folderName < f->_folderName;
}

/**
 * the value for the map function and the MapReduceFramework
 */
class SearchArg : public v1Base {
public:
    string _searchSubstring;

    SearchArg(string searchSubstring);
    ~SearchArg();
};

SearchArg::SearchArg(string searchSubstring) :
        _searchSubstring(searchSubstring) {}
SearchArg::~SearchArg() {}

//intermediate key
//the key for the Reduce function created by the Map function
class FileName : public k2Base {
public:
    string _fileName;
    string _searchWord;

    FileName(string fileName, string searchWord);
    ~FileName();
    bool operator<(const k2Base &other) const;
};


FileName::FileName(string fileName, string searchWord) : _fileName(fileName),
                                                         _searchWord(searchWord) {}
FileName::~FileName() {}
bool FileName::operator <(const k2Base& other) const {
    FileName* f = (FileName*)&other;
    return this->_fileName < f->_fileName;
}


//intermediate key
//the key, value for the Reduce function created by the Map function
class AppearanceFlag : public v2Base {
public:
    int _flag;
    FileName* _filePtr;
    AppearanceFlag(FileName* filePtr);
    ~AppearanceFlag();
};

AppearanceFlag::AppearanceFlag(FileName* filePtr) : _flag(1),
                                                    _filePtr(filePtr) {}
AppearanceFlag::~AppearanceFlag() {}


//output key
//the key for the Reduce function created by the Map function
class MatchingFile : public k3Base {
public:
    string _fileName;

    MatchingFile(string fileName);
    ~MatchingFile();
    bool operator<(const k3Base &other) const;
};


MatchingFile::MatchingFile(string fileName) : _fileName(fileName) {}
MatchingFile::~MatchingFile() {}
bool MatchingFile::operator <(const k3Base& other) const {
    MatchingFile* m = (MatchingFile*)&other;
    return this->_fileName < m->_fileName;
}

//output value
//the value for the Reduce function created by the Map function
class NumOfFiles : public v3Base {
public:
    int _numOfFiles;

    NumOfFiles(int numOfFiles);
    ~NumOfFiles();
};

NumOfFiles::NumOfFiles(int numOfFiles) : _numOfFiles(numOfFiles){}
NumOfFiles::~NumOfFiles() {}

//implementation of the MapReduceBase class for the Map and Reduce functions
class Search : public MapReduceBase {
public:
    void Map(const k1Base *const key, const v1Base *const val) const;
    void Reduce(const k2Base *const key, const V2_LIST &vals) const;
};

/*
 * implementation of the Map function
 */
void Search::Map(const k1Base* const key, const v1Base* const val) const {
    DIR* pDir = opendir(((FolderName*)key)->_folderName.c_str());
    struct dirent *pCurrFile;
    if(pDir != NULL){
        while((pCurrFile = readdir(pDir))){
            FileName* file = new FileName(pCurrFile->d_name,
                                          ((SearchArg*)val)->_searchSubstring);
            AppearanceFlag* flag = new AppearanceFlag(file);

            Emit2(file, flag);
        }
        closedir(pDir);
    }
}

/*
 * implementation of the Reduce function
 */
void Search::Reduce(const k2Base* const key, const V2_LIST& vals) const {
    FileName* fileKey = (FileName*)key;
    size_t found = fileKey->_fileName.find(fileKey->_searchWord);
    if(found != string::npos){
        MatchingFile* matchedFile = new MatchingFile(fileKey->_fileName);
        NumOfFiles* num = new NumOfFiles(vals.size());
        Emit3(matchedFile, num);
    }
    for(auto it = vals.begin(); it != vals.end(); ++it) {
        AppearanceFlag* v2Ptr = (AppearanceFlag*)(*it);
        FileName* file = (v2Ptr->_filePtr);
        delete file;
        delete v2Ptr;
    }
}

/*
 * the main function
 */
int main(int argc, char** argv) {
    if(argc < 3) {
        cout << "Usage: <substring to search> <folders, separated by space> "
        << endl;
        exit(1);
    }

    IN_ITEMS_LIST foldersList;

    for(int i = 2; i < argc; i++){
        FolderName* folder = new FolderName(argv[i]);
        SearchArg* searchWord = new SearchArg(argv[1]);
        IN_ITEM item(folder, searchWord);
        foldersList.push_back(item);
    }
    Search mapReduce;

    OUT_ITEMS_LIST res = runMapReduceFramework(mapReduce, foldersList, 5);

    for(auto it = foldersList.begin(); it != foldersList.end(); ++it) {
        FolderName* folder = (FolderName*)(it->first);
        SearchArg* arg = (SearchArg*)(it->second);
        delete folder;
        delete arg;
    }

    for(OUT_ITEMS_LIST::iterator it = res.begin(); it != res.end(); ++it){
        for(int i = 0; i < ((NumOfFiles*)(*it).second)->_numOfFiles; i++){
            cout << ((MatchingFile*)(*it).first)->_fileName << " ";
        }
    }
    for(auto it2 = res.begin(); it2 != res.end(); ++it2) {
        delete it2->first;
        delete it2->second;
    }
    return 0;
}
