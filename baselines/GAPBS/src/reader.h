// Copyright (c) 2015, The Regents of the University of California (Regents)
// See LICENSE.txt for license details

#ifndef READER_H_
#define READER_H_

#include <iostream>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pvector.h"
#include "util.h"


/*
GAP Benchmark Suite
Class:  Reader
Author: Scott Beamer

Given filename, returns an edgelist or the entire graph (if serialized)
 - Intended to be called from Builder
 - Determines file format from the filename's suffix
 - If the input graph is serialized (.sg or .wsg), reads the graph
   directly into the returned graph instance
 - Otherwise, reads the file and returns an edgelist
*/


template <typename NodeID_, typename DestID_ = NodeID_,
          typename WeightT_ = NodeID_, bool invert = true>
class Reader {
  typedef EdgePair<NodeID_, DestID_> Edge;
  typedef pvector<Edge> EdgeList;
  std::string filename_;

 public:
  explicit Reader(std::string filename) : filename_(filename) {}

  std::string GetSuffix() {
    std::size_t suff_pos = filename_.rfind('.');
    if (suff_pos == std::string::npos) {
      std::cout << "Couldn't find suffix of " << filename_ << std::endl;
      std::exit(-1);
    }
    return filename_.substr(suff_pos);
  }

  EdgeList ReadInEL(std::ifstream &in) {
    EdgeList el;
    NodeID_ u, v;
    while (in >> u >> v) {
      el.push_back(Edge(u, v));
    }
    return el;
  }

  EdgeList ReadInWEL(std::ifstream &in) {
    EdgeList el;
    NodeID_ u;
    NodeWeight<NodeID_, WeightT_> v;
    while (in >> u >> v) {
      el.push_back(Edge(u, v));
    }
    return el;
  }

  // Note: converts vertex numbering from 1..N to 0..N-1
  EdgeList ReadInGR(std::ifstream &in) {
    EdgeList el;
    char c;
    NodeID_ u;
    NodeWeight<NodeID_, WeightT_> v;
    while (!in.eof()) {
      c = in.peek();
      if (c == 'a') {
        in >> c >> u >> v;
        el.push_back(Edge(u - 1, NodeWeight<NodeID_, WeightT_>(v.v-1, v.w)));
      } else {
        in.ignore(200, '\n');
      }
    }
    return el;
  }

  // Note: converts vertex numbering from 1..N to 0..N-1
  EdgeList ReadInMetis(std::ifstream &in, bool &needs_weights) {
    EdgeList el;
    NodeID_ num_nodes, num_edges;
    char c;
    std::string line;
    bool read_weights = false;
    while (true) {
      c = in.peek();
      if (c == '%') {
        in.ignore(200, '\n');
      } else {
        std::getline(in, line, '\n');
        std::istringstream header_stream(line);
        header_stream >> num_nodes >> num_edges;
        header_stream >> std::ws;
        if (!header_stream.eof()) {
          int32_t fmt;
          header_stream >> fmt;
          if (fmt == 1) {
            read_weights = true;
          } else if ((fmt != 0) && (fmt != 100)) {
            std::cout << "Do not support METIS fmt type: " << fmt << std::endl;
            std::exit(-20);
          }
        }
        break;
      }
    }
    NodeID_ u = 0;
    while (u < num_nodes) {
      c = in.peek();
      if (c == '%') {
        in.ignore(200, '\n');
      } else {
        std::getline(in, line);
        if (line != "") {
          std::istringstream edge_stream(line);
          if (read_weights) {
            NodeWeight<NodeID_, WeightT_> v;
            while (edge_stream >> v >> std::ws) {
              v.v -= 1;
              el.push_back(Edge(u, v));
            }
          } else {
            NodeID_ v;
            while (edge_stream >> v >> std::ws) {
              el.push_back(Edge(u, v - 1));
            }
          }
        }
        u++;
      }
    }
    needs_weights = !read_weights;
    return el;
  }

  // Note: converts vertex numbering from 1..N to 0..N-1
  // Note: weights casted to type WeightT_
  EdgeList ReadInMTX(std::ifstream &in, bool &needs_weights) {
    EdgeList el;
    std::string start, object, format, field, symmetry, line;
    in >> start >> object >> format >> field >> symmetry >> std::ws;
    if (start != "%%MatrixMarket") {
      std::cout << ".mtx file did not start with %%MatrixMarket" << std::endl;
      std::exit(-21);
    }
    if ((object != "matrix") || (format != "coordinate")) {
      std::cout << "only allow matrix coordinate format for .mtx" << std::endl;
      std::exit(-22);
    }
    if (field == "complex") {
      std::cout << "do not support complex weights for .mtx" << std::endl;
      std::exit(-23);
    }
    bool read_weights;
    if (field == "pattern") {
      read_weights = false;
    } else if ((field == "real") || (field == "double") ||
               (field == "integer")) {
      read_weights = true;
    } else {
      std::cout << "unrecognized field type for .mtx" << std::endl;
      std::exit(-24);
    }
    bool undirected;
    if (symmetry == "symmetric") {
      undirected = true;
    } else if ((symmetry == "general") || (symmetry == "skew-symmetric")) {
      undirected = false;
    } else {
      std::cout << "unsupported symmetry type for .mtx" << std::endl;
      std::exit(-25);
    }
    while (true) {
      char c = in.peek();
      if (c == '%') {
        in.ignore(200, '\n');
      } else {
        break;
      }
    }
    int64_t m, n, nonzeros;
    in >> m >> n >> nonzeros >> std::ws;
    if (m != n) {
      std::cout << m << " " << n << " " << nonzeros << std::endl;
      std::cout << "matrix must be square for .mtx" << std::endl;
      std::exit(-26);
    }
    while (std::getline(in, line)) {
      if (line.empty())
        continue;
      std::istringstream edge_stream(line);
      NodeID_ u;
      edge_stream >> u;
      if (read_weights) {
        NodeWeight<NodeID_, WeightT_> v;
        edge_stream >> v;
        v.v -= 1;
        el.push_back(Edge(u - 1, v));
        if (undirected)
          el.push_back(Edge(v.v, NodeWeight<NodeID_, WeightT_>(u - 1, v.w)));
      } else {
        NodeID_ v;
        edge_stream >> v;
        el.push_back(Edge(u - 1, v - 1));
        if (undirected)
          el.push_back(Edge(v - 1, u - 1));
      }
    }
    needs_weights = !read_weights;
    return el;
  }

  EdgeList ReadFile(bool &needs_weights) {
    Timer t;
    t.Start();
    EdgeList el;
    std::string suffix = GetSuffix();
    std::ifstream file(filename_);
    if (!file.is_open()) {
      std::cout << "Couldn't open file " << filename_ << std::endl;
      std::exit(-2);
    }
    if (suffix == ".el") {
      el = ReadInEL(file);
    } else if (suffix == ".wel") {
      needs_weights = false;
      el = ReadInWEL(file);
    } else if (suffix == ".gr") {
      needs_weights = false;
      el = ReadInGR(file);
    } else if (suffix == ".graph") {
      el = ReadInMetis(file, needs_weights);
    } else if (suffix == ".mtx") {
      el = ReadInMTX(file, needs_weights);
    } else {
      std::cout << "Unrecognized suffix: " << suffix << std::endl;
      std::exit(-3);
    }
    file.close();
    t.Stop();
    PrintTime("Read Time", t.Seconds());
    return el;
  }

  CSRGraph<NodeID_, DestID_, invert> ReadBinaryGraph() {
    if (!std::is_same<NodeID_, SGID>::value) {
      std::cout << ".bin graphs only allowed for 32bit" << std::endl;
      std::exit(-7);
    }

    struct stat sb;
    int fd = open(filename_.c_str(), O_RDONLY);
    if (fd == -1 || fstat(fd, &sb) == -1) {
      std::cout << "Couldn't open file " << filename_ << std::endl;
      if (fd != -1)
        close(fd);
      std::exit(-8);
    }
    if (sb.st_size < static_cast<off_t>(3 * sizeof(uint64_t))) {
      std::cout << "Error: .bin file too small" << std::endl;
      close(fd);
      std::exit(-10);
    }

    Timer t;
    t.Start();
    char *data = static_cast<char*>(
        mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
      std::cout << "mmap failed for file " << filename_ << std::endl;
      close(fd);
      std::exit(-9);
    }

    uint64_t *header = reinterpret_cast<uint64_t*>(data);
    uint64_t num_nodes = header[0];
    uint64_t num_edges = header[1];
    uint64_t bytes = header[2];
    uint64_t unweighted_bytes = 3 * sizeof(uint64_t) +
        (num_nodes + 1) * sizeof(uint64_t) + num_edges * sizeof(uint32_t);
    uint64_t weighted_bytes = 3 * sizeof(uint64_t) +
        (num_nodes + 1) * sizeof(uint64_t) + num_edges * 8;
    bool weighted = false;
    if (bytes == unweighted_bytes) {
      weighted = false;
    } else if (bytes == weighted_bytes) {
      weighted = true;
    } else {
      std::cout << "Error: Incorrect .bin file format" << std::endl;
      munmap(data, sb.st_size);
      close(fd);
      std::exit(-10);
    }
    if (bytes != static_cast<uint64_t>(sb.st_size)) {
      std::cout << "Error: .bin header size does not match file size"
                << std::endl;
      munmap(data, sb.st_size);
      close(fd);
      std::exit(-10);
    }

    pvector<SGOffset> offsets(num_nodes + 1);
    uint64_t *file_offsets =
        reinterpret_cast<uint64_t*>(data + 3 * sizeof(uint64_t));
    #pragma omp parallel for
    for (uint64_t i = 0; i < num_nodes + 1; i++)
      offsets[i] = static_cast<SGOffset>(file_offsets[i]);

    DestID_ *neighs = new DestID_[num_edges];
    char *edge_data = data + 3 * sizeof(uint64_t) +
        (num_nodes + 1) * sizeof(uint64_t);
    if (!weighted) {
      uint32_t *file_edges = reinterpret_cast<uint32_t*>(edge_data);
      #pragma omp parallel for
      for (uint64_t i = 0; i < num_edges; i++)
        neighs[i] = DestID_(static_cast<NodeID_>(file_edges[i]));
    } else {
      struct WeightedBinEdge {
        WeightT_ wgh;
        uint32_t idx;
      };
      static_assert(sizeof(WeightedBinEdge) == 8,
                    ".bin weighted edges must be 8 bytes");
      WeightedBinEdge *file_edges =
          reinterpret_cast<WeightedBinEdge*>(edge_data);
      #pragma omp parallel for
      for (uint64_t i = 0; i < num_edges; i++) {
        if constexpr (std::is_same<NodeID_, DestID_>::value) {
          neighs[i] = static_cast<NodeID_>(file_edges[i].idx);
        } else {
          neighs[i] = DestID_(static_cast<NodeID_>(file_edges[i].idx),
                              file_edges[i].wgh);
        }
      }
    }

    munmap(data, sb.st_size);
    close(fd);

    DestID_ **index = CSRGraph<NodeID_, DestID_>::GenIndex(offsets, neighs);
    bool symmetrized = filename_.find("_sym") != std::string::npos;
    if (symmetrized) {
      t.Stop();
      PrintTime("Read Time", t.Seconds());
      return CSRGraph<NodeID_, DestID_, invert>(num_nodes, index, neighs);
    }

    DestID_ **inv_index = nullptr;
    DestID_ *inv_neighs = nullptr;
    if (invert) {
      pvector<SGOffset> in_offsets(num_nodes + 1, 0);
      #pragma omp parallel for
      for (uint64_t i = 0; i < num_edges; i++)
        fetch_and_add(in_offsets[static_cast<NodeID_>(neighs[i]) + 1], 1);
      for (uint64_t i = 1; i < num_nodes + 1; i++)
        in_offsets[i] += in_offsets[i - 1];
      inv_neighs = new DestID_[num_edges];
      inv_index = CSRGraph<NodeID_, DestID_>::GenIndex(in_offsets, inv_neighs);
      #pragma omp parallel for
      for (uint64_t u = 0; u < num_nodes; u++) {
        for (DestID_ *it = index[u]; it < index[u + 1]; it++) {
          NodeID_ v = static_cast<NodeID_>(*it);
          SGOffset pos = fetch_and_add(in_offsets[v], 1);
          if constexpr (std::is_same<NodeID_, DestID_>::value) {
            inv_neighs[pos] = DestID_(u);
          } else {
            inv_neighs[pos] = DestID_(u, it->w);
          }
        }
      }
    }

    t.Stop();
    PrintTime("Read Time", t.Seconds());
    return CSRGraph<NodeID_, DestID_, invert>(num_nodes, index, neighs,
                                              inv_index, inv_neighs);
  }

  CSRGraph<NodeID_, DestID_, invert> ReadSerializedGraph() {
    bool weighted = GetSuffix() == ".wsg";
    if (!std::is_same<NodeID_, SGID>::value) {
      std::cout << "serialized graphs only allowed for 32bit" << std::endl;
      std::exit(-5);
    }
    if (!weighted && !std::is_same<NodeID_, DestID_>::value) {
      std::cout << ".sg not allowed for weighted graphs" << std::endl;
      std::exit(-5);
    }
    if (weighted && std::is_same<NodeID_, DestID_>::value) {
      std::cout << ".wsg only allowed for weighted graphs" << std::endl;
      std::exit(-5);
    }
    if (weighted && !std::is_same<WeightT_, SGID>::value) {
      std::cout << ".wsg only allowed for int32_t weights" << std::endl;
      std::exit(-5);
    }
    std::ifstream file(filename_);
    if (!file.is_open()) {
      std::cout << "Couldn't open file " << filename_ << std::endl;
      std::exit(-6);
    }
    Timer t;
    t.Start();
    bool directed;
    SGOffset num_nodes, num_edges;
    DestID_ **index = nullptr, **inv_index = nullptr;
    DestID_ *neighs = nullptr, *inv_neighs = nullptr;
    file.read(reinterpret_cast<char*>(&directed), sizeof(bool));
    file.read(reinterpret_cast<char*>(&num_edges), sizeof(SGOffset));
    file.read(reinterpret_cast<char*>(&num_nodes), sizeof(SGOffset));
    pvector<SGOffset> offsets(num_nodes+1);
    neighs = new DestID_[num_edges];
    std::streamsize num_index_bytes = (num_nodes+1) * sizeof(SGOffset);
    std::streamsize num_neigh_bytes = num_edges * sizeof(DestID_);
    file.read(reinterpret_cast<char*>(offsets.data()), num_index_bytes);
    file.read(reinterpret_cast<char*>(neighs), num_neigh_bytes);
    index = CSRGraph<NodeID_, DestID_>::GenIndex(offsets, neighs);
    if (directed && invert) {
      inv_neighs = new DestID_[num_edges];
      file.read(reinterpret_cast<char*>(offsets.data()), num_index_bytes);
      file.read(reinterpret_cast<char*>(inv_neighs), num_neigh_bytes);
      inv_index = CSRGraph<NodeID_, DestID_>::GenIndex(offsets, inv_neighs);
    }
    file.close();
    t.Stop();
    PrintTime("Read Time", t.Seconds());
    if (directed)
      return CSRGraph<NodeID_, DestID_, invert>(num_nodes, index, neighs,
                                                inv_index, inv_neighs);
    else
      return CSRGraph<NodeID_, DestID_, invert>(num_nodes, index, neighs);
  }
};

#endif  // READER_H_
