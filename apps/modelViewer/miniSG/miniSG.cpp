// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "miniSG.h"

#ifdef USE_IMAGEMAGICK
#define MAGICKCORE_QUANTUM_DEPTH 16
#define MAGICKCORE_HDRI_ENABLE 0
# include <Magick++.h>
using namespace Magick;
#endif

namespace ospray {
  namespace miniSG {

    Texture2D::Texture2D()
      : channels(0)
      , depth(0)
      , width(0)
      , height(0)
      , data(NULL)
    {}

    Material::Material()
    {
      // setParam( "Kd", vec3f(.7f) );
      // setParam( "Ks", vec3f(0.f) );
      // setParam( "Ka", vec3f(0.f) );
    }

    Texture2D *loadTexture(const std::string &path, const std::string &fileNameBase)
    {
      const embree::FileName fileName = path+"/"+fileNameBase;

      static std::map<std::string,Texture2D*> textureCache;
      if (textureCache.find(fileName.str()) != textureCache.end()) 
        return textureCache[fileName.str()];

      Texture2D *tex = NULL;
      const std::string ext = fileName.ext();
      if (ext == "ppm") {
        try {
          int rc, peekchar;

          // open file
          FILE *file = fopen(fileName.str().c_str(),"r");
          const int LINESZ=10000;
          char lineBuf[LINESZ+1]; 

          if (!file) 
            throw std::runtime_error("#osp:miniSG: could not open texture file '"+fileName.str()+"'.");
          
          // read format specifier:
          int format=0;
          fscanf(file,"P%i\n",&format);
          if (format != 6) 
            throw std::runtime_error("#osp:miniSG: can currently load only binary P6 subformats for PPM texture files. "
                                     "Please report this bug at ospray.github.io.");

          // skip all comment lines
          peekchar = getc(file);
          while (peekchar == '#') {
            fgets(lineBuf,LINESZ,file);
            peekchar = getc(file);
          } ungetc(peekchar,file);
        
          // read width and height from first non-comment line
          int width=-1,height=-1;
          rc = fscanf(file,"%i %i\n",&width,&height);
          if (rc != 2) 
            throw std::runtime_error("#osp:miniSG: could not parse width and height in P6 PPM file '"+fileName.str()+"'. "
                                     "Please report this bug at ospray.github.io, and include named file to reproduce the error.");
        
          // skip all comment lines
          peekchar = getc(file);
          while (peekchar == '#') {
            fgets(lineBuf,LINESZ,file);
            peekchar = getc(file);
          } ungetc(peekchar,file);
        
          // read maxval
          int maxVal=-1;
          rc = fscanf(file,"%i",&maxVal);
            peekchar = getc(file);

          if (rc != 1) 
            throw std::runtime_error("#osp:miniSG: could not parse maxval in P6 PPM file '"+fileName.str()+"'. "
                                     "Please report this bug at ospray.github.io, and include named file to reproduce the error.");
          if (maxVal != 255)
            throw std::runtime_error("#osp:miniSG: could not parse P6 PPM file '"+fileName.str()+"': currently supporting only maxVal=255 formats."
                                     "Please report this bug at ospray.github.io, and include named file to reproduce the error.");
        
          tex = new Texture2D;
          tex->width    = width;
          tex->height   = height;
          tex->channels = 3;
          tex->depth    = 1;
          tex->data     = new unsigned char[width*height*3];
          fread(tex->data,width*height*3,1,file);
          char *texels = (char *)tex->data;
        } catch(std::runtime_error e) {
          std::cerr << e.what() << std::endl;
        }
      } else {
#ifdef USE_IMAGEMAGICK
        Magick::Image image(fileName.str().c_str());
        // Image* out = new Image4c(image.columns(),image.rows(),fileName);
        tex = new Texture2D;
        tex->width    = image.columns();
        tex->height   = image.rows();
        tex->channels = 4;
        tex->depth    = 4;
        float rcpMaxRGB = 1.0f/float(MaxRGB);
        Magick::Pixels pixel_cache(image);
        Magick::PixelPacket* pixels = pixel_cache.get(0,0,tex->width,tex->height);
        if (!pixels) {
          std::cerr << "#osp:minisg: failed to load texture '"+fileName.str()+"'" << std::endl;
          delete tex; 
          tex = NULL;
        } else {
          tex->data = new vec4f[tex->width*tex->height];
          for (size_t y=0; y<tex->height; y++) {
            for (size_t x=0; x<tex->width; x++) {
              vec4f c;
              c.x = float(pixels[y*tex->width+x].red    )*rcpMaxRGB;
              c.y = float(pixels[y*tex->width+x].green  )*rcpMaxRGB;
              c.z = float(pixels[y*tex->width+x].blue   )*rcpMaxRGB;
              c.w = float(pixels[y*tex->width+x].opacity)*rcpMaxRGB;
              ((vec4f*)tex->data)[x+y*tex->width] = c;
              //tex->set(x,y,c);
            }
          }
        }
#endif
      }
      textureCache[fileName.str()] = tex;
      return tex;
    }


    float Material::getParam(const char *name, float defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::FLOAT && "Param type mismatch" );
        return it->second->f[0];
      }

      return defaultVal;
    }
    
    vec2f Material::getParam(const char *name, vec2f defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::FLOAT_2 && "Param type mismatch" );
        return vec2f(it->second->f[0], it->second->f[1]);
      }

      return defaultVal;
    }

    vec3f Material::getParam(const char *name, vec3f defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::FLOAT_3 && "Param type mismatch" );
        return vec3f( it->second->f[0], it->second->f[1], it->second->f[2] );
      }

      return defaultVal;
    }

    vec4f Material::getParam(const char *name, vec4f defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::FLOAT_4 && "Param type mismatch" );
        return vec4f( it->second->f[0], it->second->f[1], it->second->f[2], it->second->f[3] );
      }

      return defaultVal;
    }

    int32 Material::getParam(const char *name, int32 defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::INT && "Param type mismatch" );
        return it->second->i[0];
      }

      return defaultVal;
    }

    vec2i Material::getParam(const char *name, vec2i defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::INT_2 && "Param type mismatch" );
        return vec2i(it->second->i[0], it->second->i[1]);
      }

      return defaultVal;
    }

    vec3i Material::getParam(const char *name, vec3i defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::INT_3 && "Param type mismatch" );
        return vec3i(it->second->i[0], it->second->i[1], it->second->i[2]);
      }

      return defaultVal;
    }

    vec4i Material::getParam(const char *name, vec4i defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::INT_4 && "Param type mismatch" );
        return vec4i(it->second->i[0], it->second->i[1], it->second->i[2], it->second->i[3]);
      }

      return defaultVal;
    }


    uint32 Material::getParam(const char *name, uint32 defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::UINT && "Param type mismatch" );
        return it->second->ui[0];
      }

      return defaultVal;
    }

    vec2ui Material::getParam(const char *name, vec2ui defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::UINT_2 && "Param type mismatch" );
        return vec2ui(it->second->ui[0], it->second->ui[1]);
      }

      return defaultVal;
    }

    vec3ui Material::getParam(const char *name, vec3ui defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::UINT_3 && "Param type mismatch" );
        return vec3ui(it->second->ui[0], it->second->ui[1], it->second->ui[2]);
      }

      return defaultVal;
    }

    vec4ui Material::getParam(const char *name, vec4ui defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::UINT_4 && "Param type mismatch" );
        return vec4ui(it->second->ui[0], it->second->ui[1], it->second->ui[2], it->second->ui[3]);
      }

      return defaultVal;
    }

    const char *Material::getParam(const char *name, const char *defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::STRING && "Param type mismatch" );
        return it->second->s;
      }

      return defaultVal;
    }

    void *Material::getParam(const char *name, void *defaultVal) 
    {
      ParamMap::iterator it = params.find(name);
      if (it != params.end()) {
        assert( it->second->type == Param::TEXTURE /*|| other 'data' types*/&& "Param type mismatch" );
        return it->second->ptr;
      }

      return defaultVal;
    }

    void error(const std::string &err)
    {
      throw std::runtime_error("ospray::miniSG fatal error : "+err);
    }

    bool operator==(const Instance &a, const Instance &b)
    { return a.meshID == b.meshID && a.xfm == b.xfm; }

    bool operator!=(const Instance &a, const Instance &b)
    { return !(a==b); }

    
    /*! computes and returns the world-space bounding box of given mesh */
    box3f Mesh::getBBox() 
    {
      if (bounds.empty()) {
        for (int i=0;i<position.size();i++)
          bounds.extend(position[i]);
      }
      return bounds;
    }

    /*! computes and returns the world-space bounding box of the entire model */
    box3f Model::getBBox() 
    {
      // this does not yet properly support instancing with transforms!
      box3f bBox = embree::empty;
      if (!instance.empty()) {
        std::vector<box3f> meshBounds;
        for (int i=0;i<mesh.size();i++)
          meshBounds.push_back(mesh[i]->getBBox());
        for (int i=0;i<instance.size();i++) {
          box3f b_i = meshBounds[instance[i].meshID];
          vec3f corner;
          for (int iz=0;iz<2;iz++) {
            corner.z = iz?b_i.upper.z:b_i.lower.z;
            for (int iy=0;iy<2;iy++) {
              corner.y = iy?b_i.upper.y:b_i.lower.y;
              for (int ix=0;ix<2;ix++) {
                corner.x = ix?b_i.upper.x:b_i.lower.x;
                bBox.extend(xfmPoint(instance[i].xfm,corner));
              }
            }
          }
        }
      } else {
        for (int i=0;i<mesh.size();i++)
          bBox.extend(mesh[i]->getBBox());
      }
      return bBox;
    }

    size_t Model::numUniqueTriangles() const
    {
      size_t sum = 0;
      for (int i=0;i<mesh.size();i++)
        sum += mesh[i]->triangle.size();
      return sum;
    }

  } // ::ospray::minisg
} // ::ospray
