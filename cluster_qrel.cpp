#include <iostream>
#include <boost/format.hpp>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <map>
#include <boost/lexical_cast.hpp>
#include <set>
#include <vector>
#include <algorithm>

using namespace std;

void load_clusters(const string& cluster_file, unordered_map<string, string>* doc2cluster, unordered_map<string, size_t>* cluster2size) {
    ifstream in(cluster_file);
    string line;
    vector<string> tokens;
    while (getline(in, line)) {
        tokens.clear();
        boost::split(tokens, line, boost::is_any_of(","));
        if (tokens.size() < 2) {
            cerr << boost::format("%1: can not parse line '%2%'") % cluster_file % line << endl;
            continue;
        }
        string& docid = tokens[0];
        string& clusterid = tokens[1];
        if (!doc2cluster->insert(make_pair(docid, clusterid)).second) {
            cerr << boost::format("document %1% has more than one cluster label") % docid << endl;
            continue;
        }
        auto it = cluster2size->find(clusterid);
        if (it == cluster2size->end()) {
            it = cluster2size->insert(it, make_pair(clusterid, 0));
        }
        it->second++;
    }
}

void load_topics(const string& qrel_file, map<string, set<string>>* topic2relevant_docids) {
    ifstream in(qrel_file);
    string line;
    vector<string> tokens;
    while (getline(in, line)) {
        tokens.clear();
        boost::split(tokens, line, boost::is_any_of(" "));
        if (tokens.size() < 4) {
            cerr << boost::format("%1%: can not parse line '%2%'") % qrel_file % line << endl;
            continue;
        }
        string& topicid = tokens[0];
        string& docid = tokens[2];
        int relevance = boost::lexical_cast<int>(tokens[3]);
        if (relevance > 0) {
            auto it = topic2relevant_docids->find(topicid);
            if (it == topic2relevant_docids->end()) {
                it = topic2relevant_docids->insert(it, make_pair(topicid, set<string>()));
            }
            it->second.insert(docid);
        }
    }
/*    double average = 0;
    for (auto& entry : *topic2relevant_docids) {
        cerr << entry.first.c_str() << " has " << entry.second.size() << " relevant documents" << endl;
        average += entry.second.size();
    }
    average /= topic2relevant_docids->size();
    cerr << "average topic size = " << average;
*/
}

struct score_t {
    vector<double> relevant;
    vector<double> size;
};

void output_score(map<string, score_t>& topic2score, unordered_map<string, string>* doc2cluster) {
    // coonvert scores to cumulative fractions
    for (auto& entry : topic2score) {
        auto convert = [](vector<double>& v, double total) {
            double cumulative_sum = 0;
            transform(v.begin(), v.end(), v.begin(), [&](double d) {
                cumulative_sum += d; 
                return cumulative_sum;
            } );
            transform(v.begin(), v.end(), v.begin(), [&](double d) { return d / total; } );
        };
        score_t& score = entry.second;
        double total_relevant = accumulate(score.relevant.begin(), score.relevant.end(), 0);
        convert(score.relevant, total_relevant);
        double total_docs = doc2cluster->size();
        convert(score.size, total_docs);
    }

    // average over all topics
    score_t average_score;
    size_t max_size = 0;
    for (auto& entry : topic2score) {
        max_size = max(max_size, entry.second.relevant.size());
    }
    average_score.relevant.resize(max_size, 0);
    average_score.size.resize(max_size, 0);
    for (auto& entry : topic2score) {
        score_t& score = entry.second;
        if (score.relevant.size() < max_size) {
            score.relevant.resize(max_size, 1);
            score.size.resize(max_size, *(--score.size.end()));
        }
        for (size_t i = 0; i < max_size; i++) {
            average_score.relevant[i] += score.relevant[i];
            average_score.size[i] += score.size[i];
        }
    }
    double total_topics = topic2score.size();
    cout << "percent of recall,0";
    for (size_t i = 0; i < average_score.relevant.size(); i++) {
        average_score.relevant[i] *= 100; 
        average_score.relevant[i] /= total_topics; 
        cout << "," << average_score.relevant[i];
    }
    cout << endl;
    cout << "percent of collection searched,0";
    for (size_t i = 0; i < average_score.size.size(); i++) {
        average_score.size[i] *= 100; 
        average_score.size[i] /= total_topics; 
        cout << "," << average_score.size[i];
    }
    cout << endl;
}

void score(unordered_map<string, string>* doc2cluster, map<string, set<string>>* topic2relevant_docids, unordered_map<string, size_t>* cluster2size) {
    map<string, score_t> topic2score;

    // process all topics
    for (auto& entry : *topic2relevant_docids) {
        const string& topicid = entry.first;
        const set<string>& relevant_docids = entry.second;
        
        // accumulate relevant count in cluster
        unordered_map<string, int> cluster2relevant_count;
        for (auto& docid : relevant_docids) {
            auto itdoc = doc2cluster->find(docid);
            if (itdoc == doc2cluster->end()) {
                cerr << boost::format("can not find cluster for docid = %1%") % docid << endl;
                continue;
            }
            auto& clusterid = itdoc->second; 
            auto it = cluster2relevant_count.find(clusterid);
            if (it == cluster2relevant_count.end()) {
                it = cluster2relevant_count.insert(it, make_pair(clusterid, 0));
            }
            it->second++;
        }

        // sort by relevant document count
        vector<pair<string, int>> relevant_count;
        for (auto& entry : cluster2relevant_count) {
            relevant_count.push_back(entry);
        }
        sort(relevant_count.begin(), relevant_count.end(), [](const pair<string, int>& l, const pair<string, int>& r) { return l.second > r.second; });

        // collect relevant document counts and cluster sizes
        score_t score;
        for (auto& entry : relevant_count) {
            score.relevant.push_back(entry.second);
        }
        for (auto& entry : relevant_count) {
            const string& clusterid = entry.first;
            size_t cluster_size = (*cluster2size)[clusterid];
            score.size.push_back(cluster_size);
        }

        // store score
        double total_relevant = accumulate(score.relevant.begin(), score.relevant.end(), 0);
        if (total_relevant > 0) { 
            topic2score.insert(make_pair(topicid, score));
        }
    }

    output_score(topic2score, doc2cluster);
}

void score_best_case(unordered_map<string, size_t>* cluster2size, map<string, set<string>>* topic2relevant_docids, unordered_map<string, string>* doc2cluster) {
    double average_cluster_size = doc2cluster->size() / (double)cluster2size->size(); 
    map<string, score_t> topic2score;

    // process all topics
    for (auto& entry : *topic2relevant_docids) {
        const string& topicid = entry.first;
        const set<string>& relevant_docids = entry.second;
        if (relevant_docids.size() > 0) {
            score_t score;
            score.relevant.push_back(relevant_docids.size()); // best case is all relevant documents are in first cluster of average size
            score.size.push_back(average_cluster_size);
            topic2score.insert(make_pair(topicid, score));
        }
    }

    output_score(topic2score, doc2cluster);
}

void score_worst_case(unordered_map<string, size_t>* cluster2size, map<string, set<string>>* topic2relevant_docids, unordered_map<string, string>* doc2cluster) {
    double average_cluster_size = doc2cluster->size() / (double)cluster2size->size(); 
    map<string, score_t> topic2score;
    vector<size_t> cluster_sizes;
    for (auto& entry : *cluster2size) {
      cluster_sizes.push_back(entry.second);
    }
    sort(cluster_sizes.begin(), cluster_sizes.end(), [](size_t l, size_t r) { return l > r; });

    // process all topics
    for (auto& entry : *topic2relevant_docids) {
        const string& topicid = entry.first;
        const set<string>& relevant_docids = entry.second;
        if (relevant_docids.size() > 0) {
            //random_shuffle(cluster_sizes.begin(), cluster_sizes.end());
            score_t score;
            for (size_t i = 0; i < relevant_docids.size(); i++) {
                score.relevant.push_back(1); // worst case is one relevant document in order or largest to smallest clusters
                if (i < cluster_sizes.size()) {
                    score.size.push_back(cluster_sizes[i]);
                } else { 
                    score.size.push_back(cluster_sizes.back());
                }
            }
            topic2score.insert(make_pair(topicid, score));
        }
    }

    output_score(topic2score, doc2cluster);
}

void make_baseline(unordered_map<string, string>* baseline_doc2cluster, unordered_map<string, string>* doc2cluster, unordered_map<string, size_t>* cluster2size) {
    srand(doc2cluster->size()); // always generate the same random baseline
    vector<const string*> docids;
    docids.reserve(doc2cluster->size());
    baseline_doc2cluster->reserve(doc2cluster->size());
    for (auto& entry : *doc2cluster) {
        docids.push_back(&entry.first);
    }
    random_shuffle(docids.begin(), docids.end());
    size_t begin = 0;
    for (auto& entry : *cluster2size) {
        size_t end = begin + entry.second;
        for (size_t i = begin; i < end; i++) {
            baseline_doc2cluster->insert(make_pair(*docids[i], entry.first));
        }
        begin = end;
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        cout << boost::format("usage: %1% [in: single lable cluster file] [in: qrel file]") % argv[0] << endl;
        return EXIT_FAILURE;
    }

    string qrel_file(argv[2]);
    map<string, set<string>> topic2relevant_docids;
    load_topics(qrel_file, &topic2relevant_docids);
    //for (auto& entry : topic2relevant_docids) {
    //    cerr << boost::format("topic %1% contains %2% relevant documents") % entry.first % entry.second.size() << endl;
    //}
    cerr << boost::format("loaded %1% topics") % topic2relevant_docids.size() << endl;

    string cluster_file(argv[1]);
    unordered_map<string, string> doc2cluster;
    unordered_map<string, size_t> cluster2size;
    load_clusters(cluster_file, &doc2cluster, &cluster2size);
    cerr << boost::format("loaded %1% documents cluster info") % doc2cluster.size() << endl;
    
    // output total number of documents
    cout << "documents," << doc2cluster.size() << endl;

    unordered_map<string, string> baseline_doc2cluster;
    make_baseline(&baseline_doc2cluster, &doc2cluster, &cluster2size);
    
    // output scores for each topic
    cout << "name," << cluster_file << endl;
    score(&doc2cluster, &topic2relevant_docids, &cluster2size);
    cout << "name,random baseline" << endl;
    score(&baseline_doc2cluster, &topic2relevant_docids, &cluster2size);
    cout << "name,best case all clusters average cluster size" << endl;
    score_best_case(&cluster2size, &topic2relevant_docids, &doc2cluster); 
    cout << "name,worst case clusters ordered largest to smallest" << endl;
    score_worst_case(&cluster2size, &topic2relevant_docids, &doc2cluster); 

    cerr << "submission documents = " << doc2cluster.size() << endl;
    cerr << "baseline documents = " << baseline_doc2cluster.size() << endl;
}
