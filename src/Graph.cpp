//
// Created by carlos on 27/05/19.
//

#include "Graph.h"

// data structures for shortest path problem with resource constraint
// ResourceContainer model
struct spp_spp_res_cont_prep {
    spp_spp_res_cont_prep(int c = 0, int r = 0) : cost(c), res(r) {}

    spp_spp_res_cont_prep &operator=(const spp_spp_res_cont_prep &other) {
        if (this == &other) return *this;
        this->~spp_spp_res_cont_prep();
        new(this) spp_spp_res_cont_prep(other);
        return *this;
    }

    int cost;
    int res;
};

bool operator==(const spp_spp_res_cont_prep &res_cont_1, const spp_spp_res_cont_prep &res_cont_2) {
    return (res_cont_1.cost == res_cont_2.cost && res_cont_1.res == res_cont_2.res);
}

bool operator<(const spp_spp_res_cont_prep &res_cont_1, const spp_spp_res_cont_prep &res_cont_2) {
    if (res_cont_1.cost > res_cont_2.cost) return false;
    if (res_cont_1.cost == res_cont_2.cost) return res_cont_1.res < res_cont_2.res;
    return true;
}

// ResourceExtensionFunction model
class ref_spprc_prep {
public:
    inline bool operator()(const SPPRCGraphPrep &g, spp_spp_res_cont_prep &new_cont, const spp_spp_res_cont_prep &old_cont,
                           graph_traits<SPPRCGraphPrep>::edge_descriptor ed) const {

        const SPPRC_Graph_Arc_Prep &arc_prop = get(edge_bundle, g)[ed];
        const SPPRC_Graph_Vert_Prep &vert_prop = get(vertex_bundle, g)[target(ed, g)];
        new_cont.cost = old_cont.cost + arc_prop.cost;
        int &i_res = new_cont.res;
        i_res = old_cont.res + arc_prop.res;
        return i_res <= vert_prop.con;
    }
};

// DominanceFunction model
class dominance_spptw_prep {
public:
    inline bool operator()(const spp_spp_res_cont_prep &res_cont_1, const spp_spp_res_cont_prep &res_cont_2) const {
        return res_cont_1.cost <= res_cont_2.cost && res_cont_1.res <= res_cont_2.res;
    }
};
// end data structures for shortest path problem with time windows (spptw)

Graph::Graph(string instance, string param, string outputName) {
    int u, v;
    double delay, jitter, bandwidth, ldp, paramDelayToken, paramJitterToken, paramVariationToken, paramBandwidthToken;
    int delayInt, jitterInt;
    string token;
    ifstream fileGraph, fileParam;
    ofstream output;

    output.open(outputName);

    fileParam.open(param, fstream::in);

    while (!fileParam.eof()) {
        fileParam >> token;
        if (token == "Delay") {
            fileParam >> token;
            if (token == "variation") {
                fileParam >> token >> paramVariationToken;
                Graph::paramVariation = int(1e5 * paramVariationToken);
            } else {
                fileParam >> paramDelayToken;
                Graph::paramDelay = int(1e5 * paramDelayToken);
            }
        }
        if (token == "Jitter") {
            fileParam >> token >> paramJitterToken;
            Graph::paramJitter = int(1e6 * paramJitterToken);
        }
        if (token == "Bandwidth") {
            fileParam >> token >> paramBandwidthToken;
            Graph::paramBandwidth = int(paramBandwidthToken);
        }
    }

    fileGraph.open(instance, fstream::in);

    while (!fileGraph.eof()) {
        fileGraph >> token;
        if (token == "Nodes") {
            fileGraph >> n;
            output << n << "\n";
            preProcessing = BoostGraph(n);
            arcs = vector<vector<Arc *>>(n, vector<Arc *>());
            removed = vector<bool>(n);
            removedF = vector<vector<vector<bool >>>(n, vector<vector<bool >>(n, vector<bool>(n)));

        }

        if (token == "Edges") {
            fileGraph >> m;
            output << m << "\n";
        }

        if (token == "E") {
            fileGraph >> u >> v >> delay >> jitter >> bandwidth >> ldp;
            if (bandwidth >= paramBandwidth) {
                delayInt = int(1e5 * delay), jitterInt = int(1e6 * jitter), --u, --v;
                Arc *arc = new Arc(u, v, delayInt, jitterInt, int(bandwidth), int(10 * ldp));
                Arc *arcRev = new Arc(v, u, delayInt, jitterInt, int(bandwidth), int(10 * ldp));
                delayVector.push_back(delayInt), jitterVector.push_back(jitterInt);
                arcs[u].push_back(arc), arcs[v].push_back(arcRev);
                add_edge(u, v, delayInt, preProcessing), add_edge(v, u, delayInt, preProcessing);
            }
        }
        if (token == "Root") fileGraph >> root, root--;
        if (token == "T") fileGraph >> u, u--, terminals.push_back(u), DuS.push_back(u);
    }

    property_map<BoostGraph, edge_weight_t>::type weightMapDelay = get(edge_weight, preProcessing);
    predecessors = vector<VertexDescriptor>(n);
    distance = vector<int>(n);

    dijkstra_shortest_paths(preProcessing, root, predecessor_map(
            make_iterator_property_map(predecessors.begin(), get(vertex_index, preProcessing))).distance_map(
            make_iterator_property_map(distance.begin(), get(vertex_index, preProcessing))));

    cntRemoved = n;
    for (int i = 0; i < n; i++) {
        removed[i] = distance[i] >= numeric_limits<int>::max();
        if (removed[i]) cntRemoved--;
    }

    bool isTerminal;
    for (int i = 0; i < n; ++i) {
        isTerminal = false;
        if (i != root && !removed[i]) {
            for (auto t : terminals) {
                if (i == t) {
                    isTerminal = true;
                    break;
                }
            }
            if (!isTerminal) nonTerminals.push_back(i), DuS.push_back(i);
        }
    }

    sort(delayVector.begin(), delayVector.end(), greater<int>());
    sort(jitterVector.begin(), jitterVector.end(), greater<int>());

    for (int i = 0; i < cntRemoved - 1; i++)
        bigMDelay += delayVector[i], bigMJitter += jitterVector[i];

    output.close();
    noPath = vector<bool>(n);
    cout << "Load graph successfully" << endl;
}

void Graph::MVE() {
    // Moterated Vertex Elimination
    int countEdges = 0, j, minSP;
    vector<int> distanceAux = vector<int>(n);
    SPPRCGraphPrep graphJitterMae;
    BoostGraph graphJitterSP = BoostGraph(n);
    vector<int> distanceJitter;

    for (int i = 0; i < n; i++)
        if(!removed[i])
            add_vertex(SPPRC_Graph_Vert_Prep(i, paramDelay), graphJitterMae);
    
    for (int u = 0; u < n; ++u)
        if (!removed[u])
            for (auto arc : arcs[u]) 
                if (!removed[arc->getD()]) {
                    add_edge(u, arc->getD(), SPPRC_Graph_Arc_Prep(countEdges++, arc->getJitter(), arc->getDelay()), graphJitterMae);
                    add_edge(u, arc->getD(), arc->getJitter(), graphJitterSP);
                }    
        
    vector<vector<graph_traits<SPPRCGraphPrep>::edge_descriptor>> opt_solutions;
    vector<spp_spp_res_cont_prep> pareto_opt; 

    bool rem;
    for (auto i : nonTerminals) {
        if (!removed[i]) {
            SPPRC_Graph_Vert_Prep &vert_prop = get(vertex_bundle, graphJitterMae)[i];
            vert_prop.con = paramDelay;

            r_c_shortest_paths(graphJitterMae,
                get(&SPPRC_Graph_Vert_Prep::num, graphJitterMae),
                get(&SPPRC_Graph_Arc_Prep::num, graphJitterMae),
                root,
                i,
                opt_solutions,
                pareto_opt,
                spp_spp_res_cont_prep(0, 0),
                ref_spprc_prep(),
                dominance_spptw_prep(),
                allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                default_r_c_shortest_paths_visitor());
            
            if (pareto_opt.empty()) {
                removed[i] = true;
            } else {
                distanceJitter = vector<int>(n);
            
                minSP = pareto_opt[0].cost;
                for (j = 1; j < int(pareto_opt.size()); j++) {
                    if (pareto_opt[j].cost < minSP) {
                        minSP = pareto_opt[j].cost;
                    }
                }

                rem = true;
                dijkstra_shortest_paths(graphJitterSP, i, predecessor_map(make_iterator_property_map(predecessors.begin(), get(vertex_index, graphJitterSP))).distance_map(
                        make_iterator_property_map(distanceJitter.begin(), get(vertex_index, graphJitterSP))));
                
                for (auto k : terminals) {
                    if (minSP + distanceJitter[k] <= paramJitter) {
                        rem = false;
                        break;
                    }
                }
                removed[i] = rem;
            }
        }
    }

    bool needClean = false;
    int aux;
    for (int j : nonTerminals) {
        if (removed[j]) {
            needClean = true;
            arcs[j].erase(arcs[j].begin(), arcs[j].end());
        }
        for (int i = 0; i < int(arcs[j].size()); i++) {
            if (removed[arcs[j][i]->getD()]) {
                arcs[j].erase(arcs[j].begin() + i);
                i--;
            }
        }
    }

    if (needClean) {
        BoostGraph graphAux = BoostGraph(n);
        distanceJitter = vector<int>(n);

        for (int i = 0; i < n; i++) 
            for (auto arc : arcs[i])
                add_edge(i, arc->getD(), arc->getJitter(), graphAux);     
        
        dijkstra_shortest_paths(graphAux, root, predecessor_map(
                make_iterator_property_map(predecessors.begin(), get(vertex_index, graphAux))).distance_map(
                make_iterator_property_map(distanceJitter.begin(), get(vertex_index, graphAux))));
 
        for (j = 0; j < n; j++) 
            if (!removed[j] && distanceJitter[j] >= numeric_limits<int>::max()) 
                removed[j] = true;

        for (j = 0; j < n; j++) {
            if (removed[j]) 
                arcs[j].erase(arcs[j].begin(), arcs[j].end());
            
            for (int i = 0; i < int(arcs[j].size()); i++) {
                if (removed[arcs[j][i]->getD()]) 
                    arcs[j].erase(arcs[j].begin() + i--);
            }
        }   
    }

    cntRemoved = n;
    for (int i = 0; i < n; i++) 
        if (removed[i]) cntRemoved--;
    
    for (int i = 0; i < nonTerminals.size(); i++) 
        if (removed[nonTerminals[i]])
            nonTerminals.erase(nonTerminals.begin() + i--); 
    
    for (int i = 0; i < DuS.size(); i++) 
        if (removed[DuS[i]])
            DuS.erase(DuS.begin() + i--); 

    bigMDelay = 0, bigMJitter = 0;
    for (int i = 0; i < cntRemoved - 1; i++)
        bigMDelay += delayVector[i], bigMJitter += jitterVector[i];

    cout << "MVE preprocessing finished!" << endl;
}

void Graph::SAE() {
    int i, u, j, minSP, countEdges = 0;
    vector<int> jitterFromCShp = vector<int>(n), delayFromCShp = vector<int>(n), minSPVec = vector<int>(n);
    vector<int> distanceJitter;
    vector<vector<int>> CSHP = vector<vector<int>>(n, vector<int>(n));
    BoostGraph graphJitterSP = BoostGraph(n);
    SPPRCGraphPrep graphDelay, graphJitter;

    for (i = 0; i < n; i++)
        if (!removed[i])
            for (auto arc : arcs[i]) 
                add_edge(i, arc->getD(), arc->getJitter(), graphJitterSP);
                

    for (i = 0; i < n; i++) {
        distanceJitter = vector<int>(n);

        dijkstra_shortest_paths(graphJitterSP, i, predecessor_map(
                make_iterator_property_map(predecessors.begin(), get(vertex_index, graphJitterSP))).distance_map(
                make_iterator_property_map(distanceJitter.begin(), get(vertex_index, graphJitterSP))));

        minSP = paramJitter;
        for (auto t : terminals)
            if (t != i && distanceJitter[t] < minSP)
                minSP = distanceJitter[t];
        minSPVec[i] = minSP;

        add_vertex(SPPRC_Graph_Vert_Prep(i, paramJitter), graphDelay);
        add_vertex(SPPRC_Graph_Vert_Prep(i, paramDelay), graphJitter);
    }

    for (u = 0; u < n; ++u) {
        if (!removed[u]) {
            for (auto arc : arcs[u]) {
                j = arc->getD();
                add_edge(u, j, SPPRC_Graph_Arc_Prep(countEdges, arc->getDelay(), arc->getJitter()), graphDelay);
                add_edge(u, j, SPPRC_Graph_Arc_Prep(countEdges++, arc->getJitter(), arc->getDelay()), graphJitter);  
            }
        }
    } 

    // Calculation of Constrained Shortests Paths
    vector<vector<graph_traits<SPPRCGraphPrep>::edge_descriptor>> opt_solutions;
    vector<spp_spp_res_cont_prep> pareto_opt;

    // CSP root -> S (NonTerminals)
    for (auto j : nonTerminals) {
        // CSHP delay
        if (!removed[j]) {
            SPPRC_Graph_Vert_Prep &vert_prop = get(vertex_bundle, graphDelay)[j];
            vert_prop.con = paramJitter - minSPVec[j];

            r_c_shortest_paths(graphDelay,
                     get(&SPPRC_Graph_Vert_Prep::num, graphDelay),
                     get(&SPPRC_Graph_Arc_Prep::num, graphDelay),
                     root,
                     j,
                     opt_solutions,
                     pareto_opt,
                     spp_spp_res_cont_prep(0, 0),
                     ref_spprc_prep(),
                     dominance_spptw_prep(),
                     allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                     default_r_c_shortest_paths_visitor());
            if (pareto_opt.empty()) 
                delayFromCShp[j] = paramDelay;
            else {
                minSP = pareto_opt[0].cost;
                for (auto p : pareto_opt) 
                    if (p.cost < minSP)
                        minSP = p.cost;
                delayFromCShp[j] = minSP;
            }
            vert_prop.con = paramJitter;

            SPPRC_Graph_Vert_Prep &vert_prop_d = get(vertex_bundle, graphJitter)[j];
            vert_prop_d.con = paramDelay;

            // CSHP jitter
            r_c_shortest_paths(graphJitter,
                     get(&SPPRC_Graph_Vert_Prep::num, graphJitter),
                     get(&SPPRC_Graph_Arc_Prep::num, graphJitter),
                     root,
                     j,
                     opt_solutions,
                     pareto_opt,
                     spp_spp_res_cont_prep(0, 0),
                     ref_spprc_prep(),
                     dominance_spptw_prep(),
                     allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                     default_r_c_shortest_paths_visitor());
            
            if (pareto_opt.empty()) 
                jitterFromCShp[j] = paramJitter;
            else {
                minSP = pareto_opt[0].cost;
                for (auto p : pareto_opt) 
                    if (p.cost < minSP)
                        minSP = p.cost;
                jitterFromCShp[j] = minSP;
            }          
        }
    }

    // CSHP root -> j (terminals)
    for (auto j : terminals) {
        if (!removed[j]) {
            SPPRC_Graph_Vert_Prep &vert_prop = get(vertex_bundle, graphDelay)[j];
            vert_prop.con = paramJitter;

            r_c_shortest_paths(graphDelay,
                     get(&SPPRC_Graph_Vert_Prep::num, graphDelay),
                     get(&SPPRC_Graph_Arc_Prep::num, graphDelay),
                     root,
                     j,
                     opt_solutions,
                     pareto_opt,
                     spp_spp_res_cont_prep(0, 0),
                     ref_spprc_prep(),
                     dominance_spptw_prep(),
                     allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                     default_r_c_shortest_paths_visitor());
            if (pareto_opt.empty()) 
                delayFromCShp[j] = paramDelay;
            else {
                minSP = pareto_opt[0].cost;
                for (auto p : pareto_opt) 
                    if (p.cost < minSP)
                        minSP = p.cost;
                delayFromCShp[j] = minSP;
            }

            SPPRC_Graph_Vert_Prep &vert_prop_d = get(vertex_bundle, graphJitter)[j];
            vert_prop_d.con = paramDelay;
            // CSHP jitter
            r_c_shortest_paths(graphJitter,
                     get(&SPPRC_Graph_Vert_Prep::num, graphJitter),
                     get(&SPPRC_Graph_Arc_Prep::num, graphJitter),
                     root,
                     j,
                     opt_solutions,
                     pareto_opt,
                     spp_spp_res_cont_prep(0, 0),
                     ref_spprc_prep(),
                     dominance_spptw_prep(),
                     allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                     default_r_c_shortest_paths_visitor());
            
            if (pareto_opt.empty()) 
                jitterFromCShp[j] = paramJitter;
            else {
                minSP = pareto_opt[0].cost;
                for (auto p : pareto_opt) 
                    if (p.cost < minSP)
                        minSP = p.cost;
                jitterFromCShp[j] = minSP;
            }
        }
    }

    // CSHP: K -> J (NonTerminals) 
    for (auto j : DuS) {
        if (!removed[j]) {
            SPPRC_Graph_Vert_Prep &vert_prop = get(vertex_bundle, graphDelay)[j];
            vert_prop.con = paramJitter - jitterFromCShp[j];

            for (auto k : terminals) {
                if (!removed[k] && k != j) {
                    r_c_shortest_paths(graphDelay,
                            get(&SPPRC_Graph_Vert_Prep::num, graphDelay),
                            get(&SPPRC_Graph_Arc_Prep::num, graphDelay),
                            k,
                            j,
                            opt_solutions,
                            pareto_opt,
                            spp_spp_res_cont_prep(0, 0),
                            ref_spprc_prep(),
                            dominance_spptw_prep(),
                            allocator<r_c_shortest_paths_label<SPPRCGraphPrep, spp_spp_res_cont_prep >>(),
                            default_r_c_shortest_paths_visitor());
                    if (pareto_opt.empty()) 
                        CSHP[k][j] = paramDelay;
                    else {
                        minSP = pareto_opt[0].cost;
                        for (auto p : pareto_opt) 
                            if (p.cost < minSP)
                                minSP = p.cost;
                        CSHP[k][j] = minSP;
                    }
                }
            }
            vert_prop.con = paramJitter;
        }
    }

    int cntRem = 0;
    for (auto i : DuS) {
        for (auto arc : arcs[i]) {
            j = arc->getD();
            for (auto k : terminals)
                if (k != j && k != i)
                    if (delayFromCShp[i] + arc->getDelay() + CSHP[k][j] > paramDelay) {
                        cntRem++;
                        removedF[i][j][k] = true;
                    }
        }
    }

    cout << "SAE finished! " << cntRem << endl;
    finishPreprocessing();
}

void Graph::finishPreprocessing() {
    int i, j;
    vector<BoostGraph> preprocessingGraphs = vector<BoostGraph>(n);
    

    for (auto i : terminals)
        preprocessingGraphs[i] = BoostGraph(n);

    for (auto k : terminals)
        for (int i = 0; i < n; i++)
            for (auto arc : arcs[i]) 
                if (!removedF[i][arc->getD()][k])
                    add_edge(i, arc->getD(), 1, preprocessingGraphs[k]);
       
    vector<VertexDescriptor> predecessors;
    vector<int> distance;
    
    for (auto k : terminals) {
        noPath[k] = false;
        property_map<BoostGraph, edge_weight_t>::type weightMapDelay = get(edge_weight, preprocessingGraphs[k]);
        predecessors = vector<VertexDescriptor>(n);
        distance = vector<int>(n);

        dijkstra_shortest_paths(preprocessingGraphs[k], root, predecessor_map(
            make_iterator_property_map(predecessors.begin(), get(vertex_index, preprocessingGraphs[k]))).distance_map(
            make_iterator_property_map(distance.begin(), get(vertex_index, preprocessingGraphs[k]))));

        if (distance[k] >= numeric_limits<int>::max()) {
            noPath[k] = true;
            cntRemoved--;
        }
    }
    
    bigMDelay = 0, bigMJitter = 0;
    for (int i = 0; i < cntRemoved - 1; i++)
        bigMDelay += delayVector[i], bigMJitter += jitterVector[i];
    cout << "Finishing preprocessing" << endl;
}

void Graph::showGraph() {
    cout << "Arcs" << endl;
    for (int o = 0; o < n; o++) {
        if (!removed[o]) {
            for (auto *arc : arcs[o])
                cout << arc->getO()+1 << " " << arc->getD()+1 << ": " << arc->getDelay()
                     << " " << arc->getJitter() << " " << arc->getBandwidth() << " " <<
                     arc->getEstimateLinkDuration() << endl;
        }
    }

    cout << "\n Param" << endl;
    cout << "Nodes: " << n << " Edges: " << m <<
         " CntTerminals: " << int(terminals.size()) << " Root: " << root << endl;

    cout << "Delay: " << paramDelay << " Jitter: " << paramJitter <<
         " DelayVari.: " << paramVariation << " Bandwidth: " << paramBandwidth << endl;

    cout << "Terminals" << endl;
    for (int i : terminals) {
        cout << "T: " << i+1 << " ";
    }
    cout << "\nNonTerminals" << endl;
    for (int i : nonTerminals) {
        cout << "NT: " << i+1 << " ";
    }
}

int Graph::getBigMDelay() {
    return bigMDelay;
}

int Graph::getBigMJitter() {
    return bigMJitter;
}

int Graph::getShpTerminal(int k) {
    return distance[k];
}

int Graph::getN() const {
    return n;
}

void Graph::setN(int n) {
    Graph::n = n;
}

int Graph::getM() const {
    return m;
}

void Graph::setM(int m) {
    Graph::m = m;
}

int Graph::getParamDelay() const {
    return paramDelay;
}

void Graph::setParamDelay(int paramDelay) {
    Graph::paramDelay = paramDelay;
}

int Graph::getParamJitter() const {
    return paramJitter;
}

void Graph::setParamJitter(int paramJitter) {
    Graph::paramJitter = paramJitter;
}

int Graph::getParamVariation() const {
    return paramVariation;
}

int Graph::getNAfterRemoved() {
    return cntRemoved;
}

void Graph::setParamVariation(int paramVariation) {
    Graph::paramVariation = paramVariation;
}

int Graph::getParamBandwidth() const {
    return paramBandwidth;
}

void Graph::setParamBandwidth(int paramBandwidth) {
    Graph::paramBandwidth = paramBandwidth;
}

int Graph::getRoot() const {
    return root;
}

void Graph::setRoot(int root) {
    Graph::root = root;
}

int Graph::getDelay(int i, int j) {
    for (auto arc : arcs[i]) {
        if (arc->getD() == j) {
            return arc->getDelay();
        }
    }
    return paramDelay;
}

int Graph::getJitter(int i, int j) {
    for (auto arc : arcs[i]) {
        if (arc->getD() == j) {
            return arc->getJitter();
        }
    }
    return paramJitter;
}